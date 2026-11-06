/*
Copyright (C) 2008 r1ch.net

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#define CURL_DISABLE_DEPRECATION

#include "client.h"
#include <assert.h>

#if USE_CURL_DLOPEN
// curl.h turns curl_easy_setopt() and friends into type checking macros when
// compiling with GCC. They have to stay plain function names here, because
// they are redirected to pointers loaded at runtime below.
#define CURL_DISABLE_TYPECHECK
#endif
#include <curl/curl.h>

#include "shared/atomic.h"
#include "system/pthread.h"

static cvar_t  *cl_http_downloads;
static cvar_t  *cl_http_filelists;
static cvar_t  *cl_http_max_connections;
static cvar_t  *cl_http_proxy;
static cvar_t  *cl_http_default_url;
static cvar_t  *cl_http_insecure;

#if USE_DEBUG
static cvar_t  *cl_http_debug;
#endif

#if USE_UI
static cvar_t  *cl_http_blocking_timeout;
#endif

// size limits for filelists, must be power of two
#define MAX_DLSIZE  (1 << 20)   // 1 MiB
#define MIN_DLSIZE  (1 << 15)   // 32 KiB

#define INSANE_SIZE (1LL << 40)

#define MAX_DLHANDLES   16  //for multiplexing

typedef struct {
    CURL        *curl;
    char        path[MAX_OSPATH];
    FILE        *file;
    dlqueue_t   *queue;
    size_t      size;
    size_t      position;
    char        *buffer;
    CURLcode    result;
    atomic_int  state;
} dlhandle_t;

static dlhandle_t   download_handles[MAX_DLHANDLES];    //actual download handles
static char         download_server[512];    //base url prefix to download from
static char         download_referer[32];    //libcurl no longer requires a static string ;)
static bool         download_default_repo;

static pthread_mutex_t  progress_mutex;
static dlqueue_t        *download_current;
static int64_t          download_position;
static int              download_percent;

static bool         curl_initialized;
static CURLM        *curl_multi;

#if USE_CURL_DLOPEN

/*
libcurl is loaded at runtime instead of being linked, for binaries that must
start on any Linux distribution. Distributions name the library differently
(libcurl-gnutls.so.4 on Debian and Ubuntu, libcurl.so.4 elsewhere), so a
binary linked against one of them fails to start with "error while loading
shared libraries" on the others. Loading it at runtime also means the game
still starts without libcurl; only HTTP downloads are unavailable then.
*/

#define CURL_FUNCTIONS \
    CURL_FUNC(curl_easy_cleanup) \
    CURL_FUNC(curl_easy_getinfo) \
    CURL_FUNC(curl_easy_init) \
    CURL_FUNC(curl_easy_perform) \
    CURL_FUNC(curl_easy_setopt) \
    CURL_FUNC(curl_easy_strerror) \
    CURL_FUNC(curl_global_cleanup) \
    CURL_FUNC(curl_global_init) \
    CURL_FUNC(curl_multi_add_handle) \
    CURL_FUNC(curl_multi_cleanup) \
    CURL_FUNC(curl_multi_info_read) \
    CURL_FUNC(curl_multi_init) \
    CURL_FUNC(curl_multi_perform) \
    CURL_FUNC(curl_multi_poll) \
    CURL_FUNC(curl_multi_remove_handle) \
    CURL_FUNC(curl_multi_setopt) \
    CURL_FUNC(curl_multi_strerror) \
    CURL_FUNC(curl_multi_wakeup) \
    CURL_FUNC(curl_version)

#define CURL_FUNC(name) static __typeof__(name) *q##name;
CURL_FUNCTIONS
#undef CURL_FUNC

static void *curl_library;

static void unload_curl(void)
{
    if (curl_library) {
        Sys_FreeLibrary(curl_library);
        curl_library = NULL;
    }
}

static bool load_curl(void)
{
    static const char *const names[] = { "libcurl.so.4", "libcurl-gnutls.so.4" };
    int i;

    for (i = 0; i < q_countof(names); i++) {
        Sys_LoadLibrary(names[i], NULL, &curl_library);
        if (curl_library)
            break;
    }
    if (!curl_library) {
        Com_Printf("libcurl not found, HTTP downloads are disabled. "
                   "Install your distribution's libcurl package to enable them.\n");
        return false;
    }

#define CURL_FUNC(name) \
    if (!(*(void **)&q##name = Sys_GetProcAddress(curl_library, #name))) { \
        Com_EPrintf("%s lacks %s, HTTP downloads are disabled\n", names[i], #name); \
        unload_curl(); \
        return false; \
    }
    CURL_FUNCTIONS
#undef CURL_FUNC

    return true;
}

// From here on, calls go through the loaded pointers. Recent curl.h versions
// define a few of these names as function-like macros, so drop those first.
#undef curl_easy_getinfo
#undef curl_easy_setopt
#undef curl_multi_setopt
#define curl_easy_cleanup qcurl_easy_cleanup
#define curl_easy_getinfo qcurl_easy_getinfo
#define curl_easy_init qcurl_easy_init
#define curl_easy_perform qcurl_easy_perform
#define curl_easy_setopt qcurl_easy_setopt
#define curl_easy_strerror qcurl_easy_strerror
#define curl_global_cleanup qcurl_global_cleanup
#define curl_global_init qcurl_global_init
#define curl_multi_add_handle qcurl_multi_add_handle
#define curl_multi_cleanup qcurl_multi_cleanup
#define curl_multi_info_read qcurl_multi_info_read
#define curl_multi_init qcurl_multi_init
#define curl_multi_perform qcurl_multi_perform
#define curl_multi_poll qcurl_multi_poll
#define curl_multi_remove_handle qcurl_multi_remove_handle
#define curl_multi_setopt qcurl_multi_setopt
#define curl_multi_strerror qcurl_multi_strerror
#define curl_multi_wakeup qcurl_multi_wakeup
#define curl_version qcurl_version

#endif // USE_CURL_DLOPEN

static atomic_int   worker_terminate;
static atomic_int   worker_status;
static pthread_t    worker_thread;

static void *worker_func(void *arg);

/*
===============================
R1Q2 HTTP Downloading Functions
===============================
HTTP downloading is used if the server provides a content
server url in the connect message. Any missing content the
client needs will then use the HTTP server instead of auto
downloading via UDP. CURL is used to enable multiple files
to be downloaded in parallel to improve performance on high
latency links when small files such as textures are needed.
Since CURL natively supports gzip content encoding, any files
on the HTTP server should ideally be gzipped to conserve
bandwidth.
*/

// libcurl callback to update progress info.
static int progress_func(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    dlhandle_t *dl = (dlhandle_t *)clientp;

    //sanity check
    if (dlnow > INSANE_SIZE)
        return -1;

    pthread_mutex_lock(&progress_mutex);
    download_current = dl->queue;
    download_percent = dltotal ? dlnow * 100LL / dltotal : 0;
    download_position = dlnow;
    pthread_mutex_unlock(&progress_mutex);

    return 0;
}

// libcurl callback for filelists.
// must be thread safe!
static size_t recv_func(void *ptr, size_t size, size_t nmemb, void *stream)
{
    dlhandle_t *dl = (dlhandle_t *)stream;
    size_t new_size, bytes;

    if (!size || !nmemb)
        return 0;

    assert(size <= SIZE_MAX / nmemb);
    assert(dl->position < MAX_DLSIZE);

    bytes = size * nmemb;
    if (bytes >= MAX_DLSIZE - dl->position)
        return 0;

    // grow buffer in MIN_DLSIZE chunks. +1 for NUL.
    new_size = Q_ALIGN(dl->position + bytes + 1, MIN_DLSIZE);
    if (new_size > dl->size) {
        char *buf = realloc(dl->buffer, new_size);
        if (!buf)
            return 0;
        dl->size = new_size;
        dl->buffer = buf;
    }

    memcpy(dl->buffer + dl->position, ptr, bytes);
    dl->position += bytes;
    dl->buffer[dl->position] = 0;

    return bytes;
}

// Escapes most reserved characters defined by RFC 3986.
// Similar to curl_easy_escape(), but doesn't escape '/'.
static void escape_path(char *escaped, const char *path)
{
    while (*path) {
        byte c = *path++;
        if (!Q_isalnum(c) && !strchr("/-_.~", c)) {
            escaped[0] = '%';
            escaped[1] = com_hexchars[c >> 4];
            escaped[2] = com_hexchars[c & 15];
            escaped += 3;
        } else {
            *escaped++ = c;
        }
    }
    *escaped = 0;
}

// curl doesn't provide a way to convert HTTP response code to string...
static const char *http_strerror(long response)
{
    static char buffer[32];
    const char *str;

    //common codes
    switch (response) {
        case 200: return "200 OK";
        case 401: return "401 Unauthorized";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 500: return "500 Internal Server Error";
        case 503: return "503 Service Unavailable";
    }

    //generic classes
    switch (response / 100) {
        case 1:  str = "Informational"; break;
        case 2:  str = "Success";       break;
        case 3:  str = "Redirection";   break;
        case 4:  str = "Client Error";  break;
        case 5:  str = "Server Error";  break;
        default: str = "<bad code>";    break;
    }

    Q_snprintf(buffer, sizeof(buffer), "%ld %s", response, str);
    return buffer;
}

// Use "baseq2" instead of empty gamedir consistently for all kinds of downloads.
static const char *http_gamedir(void)
{
    if (*fs_game->string)
        return fs_game->string;

    return BASEGAME;
}

static const char *http_proxy(void)
{
    if (*cl_http_proxy->string)
        return cl_http_proxy->string;

    return NULL;
}

// Actually starts a download by adding it to the curl multi handle.
static bool start_download(dlqueue_t *entry, dlhandle_t *dl)
{
    size_t  len;
    char    url[576];
    char    temp[MAX_QPATH];
    char    escaped[MAX_QPATH * 4];
    int     err;

    //yet another hack to accommodate filelists, how i wish i could push :(
    //NULL file handle indicates filelist.
    if (entry->type == DL_LIST) {
        dl->file = NULL;
        dl->path[0] = 0;
        //filelist paths are absolute
        escape_path(escaped, entry->path);
    } else {
        len = Q_snprintf(dl->path, sizeof(dl->path), "%s/%s.tmp", fs_gamedir, entry->path);
        if (len >= sizeof(dl->path)) {
            Com_EPrintf("[HTTP] Refusing oversize temporary file path.\n");
            goto fail;
        }

        //prepend quake path with gamedir
        len = Q_snprintf(temp, sizeof(temp), "%s/%s", http_gamedir(), entry->path);
        if (len >= sizeof(temp)) {
            Com_EPrintf("[HTTP] Refusing oversize server file path.\n");
            goto fail;
        }
        escape_path(escaped, temp);

        err = FS_CreatePath(dl->path);
        if (err < 0) {
            Com_EPrintf("[HTTP] Couldn't create path to '%s': %s\n", dl->path, Q_ErrorString(err));
            goto fail;
        }

        //don't bother with http resume... too annoying if server doesn't support it.
        dl->file = fopen(dl->path, "wb");
        if (!dl->file) {
            Com_EPrintf("[HTTP] Couldn't open '%s' for writing: %s\n", dl->path, strerror(errno));
            goto fail;
        }
    }

    len = Q_snprintf(url, sizeof(url), "%s%s", download_server, escaped);
    if (len >= sizeof(url)) {
        Com_EPrintf("[HTTP] Refusing oversize download URL.\n");
        goto fail;
    }

    dl->buffer = NULL;
    dl->size = 0;
    dl->position = 0;
    dl->queue = entry;
    if (!dl->curl && !(dl->curl = curl_easy_init())) {
        Com_EPrintf("curl_easy_init failed\n");
        goto fail;
    }

    if (cl_http_insecure->integer) {
        curl_easy_setopt(dl->curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(dl->curl, CURLOPT_SSL_VERIFYHOST, 0L);
    } else {
        curl_easy_setopt(dl->curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(dl->curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    curl_easy_setopt(dl->curl, CURLOPT_ACCEPT_ENCODING, "");
#if USE_DEBUG
    curl_easy_setopt(dl->curl, CURLOPT_VERBOSE, cl_http_debug->integer | 0L);
#endif
    curl_easy_setopt(dl->curl, CURLOPT_NOPROGRESS, 0L);
    if (dl->file) {
        curl_easy_setopt(dl->curl, CURLOPT_WRITEDATA, dl->file);
        curl_easy_setopt(dl->curl, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(dl->curl, CURLOPT_MAXFILESIZE, 0L);
    } else {
        curl_easy_setopt(dl->curl, CURLOPT_WRITEDATA, dl);
        curl_easy_setopt(dl->curl, CURLOPT_WRITEFUNCTION, recv_func);
        curl_easy_setopt(dl->curl, CURLOPT_MAXFILESIZE, MAX_DLSIZE - 1L);
    }
    curl_easy_setopt(dl->curl, CURLOPT_PROXY, http_proxy());
    curl_easy_setopt(dl->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(dl->curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(dl->curl, CURLOPT_XFERINFOFUNCTION, progress_func);
    curl_easy_setopt(dl->curl, CURLOPT_XFERINFODATA, dl);
    curl_easy_setopt(dl->curl, CURLOPT_USERAGENT, com_version->string);
    curl_easy_setopt(dl->curl, CURLOPT_REFERER, download_referer);
    curl_easy_setopt(dl->curl, CURLOPT_URL, url);
    curl_easy_setopt(dl->curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS | 0L);
    curl_easy_setopt(dl->curl, CURLOPT_PRIVATE, dl);

    Com_DPrintf("[HTTP] Fetching %s...\n", url);
    entry->state = DL_RUNNING;
    atomic_store(&dl->state, DL_PENDING);
    return true;

fail:
    CL_FinishDownload(entry);

    // see if we have more to dl
    CL_RequestNextDownload();
    return false;
}

#if USE_UI

/*
===============
HTTP_FetchFile

Fetches data from an arbitrary URL in a blocking fashion. Doesn't touch any
global variables and thus doesn't interfere with existing client downloads.
===============
*/
int HTTP_FetchFile(const char *url, void **data)
{
    dlhandle_t tmp;
    CURL *curl;
    CURLcode ret;
    long response;

    *data = NULL;

    if (!curl_initialized) {
        Com_Printf("[HTTP] Can't fetch %s: libcurl is not available.\n", url);
        return -1;
    }

    curl = curl_easy_init();
    if (!curl)
        return -1;

    memset(&tmp, 0, sizeof(tmp));

    if (cl_http_insecure->integer) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
#if USE_DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, cl_http_debug->integer | 0L);
#endif
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &tmp);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, recv_func);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, MAX_DLSIZE - 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_PROXY, http_proxy());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, com_version->string);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS | 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, cl_http_blocking_timeout->integer | 0L);

    ret = curl_easy_perform(curl);

    if (ret == CURLE_HTTP_RETURNED_ERROR)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);

    curl_easy_cleanup(curl);

    if (ret == CURLE_OK) {
        *data = tmp.buffer;
        return tmp.position;
    }

    Com_EPrintf("[HTTP] Failed to fetch '%s': %s\n",
                url, ret == CURLE_HTTP_RETURNED_ERROR ?
                http_strerror(response) : curl_easy_strerror(ret));
    HTTP_FreeFile(tmp.buffer);
    return -1;
}

#endif

/*
===============
HTTP_CleanupDownloads

Disconnected from server, or fatal HTTP error occurred. Clean up.
===============
*/
void HTTP_CleanupDownloads(void)
{
    dlhandle_t  *dl;
    int         i;

    download_server[0] = 0;
    download_referer[0] = 0;
    download_default_repo = false;

    if (curl_multi) {
        atomic_store(&worker_terminate, true);
        curl_multi_wakeup(curl_multi);

        Q_assert(!pthread_join(worker_thread, NULL));
        pthread_mutex_destroy(&progress_mutex);

        curl_multi_cleanup(curl_multi);
        curl_multi = NULL;
    }

    for (i = 0; i < MAX_DLHANDLES; i++) {
        dl = &download_handles[i];

        if (dl->file) {
            fclose(dl->file);
            remove(dl->path);
        }

        free(dl->buffer);

        if (dl->curl)
            curl_easy_cleanup(dl->curl);
    }

    memset(download_handles, 0, sizeof(download_handles));
}

/*
===============
HTTP_Init

Init libcurl and multi handle.
===============
*/
void HTTP_Init(void)
{
    cl_http_downloads = Cvar_Get("cl_http_downloads", "1", 0);
    cl_http_filelists = Cvar_Get("cl_http_filelists", "1", 0);
    cl_http_max_connections = Cvar_Get("cl_http_max_connections", "2", 0);
    cl_http_proxy = Cvar_Get("cl_http_proxy", "", 0);
    cl_http_default_url = Cvar_Get("cl_http_default_url", "", 0);
    cl_http_insecure = Cvar_Get("cl_http_insecure", "0", 0);

#if USE_DEBUG
    cl_http_debug = Cvar_Get("cl_http_debug", "0", 0);
#endif

#if USE_UI
    cl_http_blocking_timeout = Cvar_Get("cl_http_blocking_timeout", "15", 0);
#endif

#if USE_CURL_DLOPEN
    if (!load_curl())
        return;
#endif

    if (curl_global_init(CURL_GLOBAL_NOTHING)) {
        Com_EPrintf("curl_global_init failed\n");
#if USE_CURL_DLOPEN
        unload_curl();
#endif
        return;
    }

    curl_initialized = true;
    Com_DPrintf("%s initialized.\n", curl_version());
}

void HTTP_Shutdown(void)
{
    if (!curl_initialized)
        return;

    HTTP_CleanupDownloads();

    curl_global_cleanup();
    curl_initialized = false;
#if USE_CURL_DLOPEN
    unload_curl();
#endif
}

/*
===============
HTTP_SetServer

A new server is specified, so we nuke all our state.
===============
*/
void HTTP_SetServer(const char *url)
{
    if (curl_multi) {
        Com_EPrintf("[HTTP] Set server without cleanup?\n");
        return;
    }

    if (!curl_initialized)
        return;

    // ignore on the local server
    if (NET_IsLocalAddress(&cls.serverAddress))
        return;

    // ignore if downloads are permanently disabled
    if (allow_download->integer == -1)
        return;

    // ignore if HTTP downloads are disabled
    if (cl_http_downloads->integer == 0)
        return;

    // use default URL for servers that don't specify one. treat 404 from
    // default repository as fatal error and revert to UDP downloading.
    if (!url) {
        url = cl_http_default_url->string;
        download_default_repo = true;
    } else {
        download_default_repo = false;
    }

    if (!*url)
        return;

    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) {
        Com_Printf("[HTTP] Ignoring download server URL with non-HTTP schema.\n");
        return;
    }

    if (strlen(url) >= sizeof(download_server)) {
        Com_Printf("[HTTP] Ignoring oversize download server URL.\n");
        return;
    }

    if (!(curl_multi = curl_multi_init())) {
        Com_EPrintf("curl_multi_init failed\n");
        return;
    }

    curl_multi_setopt(curl_multi, CURLMOPT_MAX_HOST_CONNECTIONS,
                      Cvar_ClampInteger(cl_http_max_connections, 1, 4) | 0L);

    pthread_mutex_init(&progress_mutex, NULL);

    worker_terminate = false;
    worker_status = 0;
    if (pthread_create(&worker_thread, NULL, worker_func, NULL)) {
        Com_EPrintf("Couldn't create curl worker thread\n");
        pthread_mutex_destroy(&progress_mutex);
        curl_multi_cleanup(curl_multi);
        curl_multi = NULL;
        return;
    }

    Q_strlcpy(download_server, url, sizeof(download_server));
    Q_snprintf(download_referer, sizeof(download_referer),
               "quake2://%s", NET_AdrToString(&cls.serverAddress));

    Com_Printf("[HTTP] Download server at %s\n", download_server);
}

/*
===============
HTTP_QueueDownload

Called from the precache check to queue a download. Return value of
Q_ERR(ENOSYS) will cause standard UDP downloading to be used instead.
===============
*/
int HTTP_QueueDownload(const char *path, dltype_t type)
{
    size_t      len;
    bool        need_list;
    char        temp[MAX_QPATH];
    int         ret;

    // no http server (or we got booted)
    if (!curl_multi)
        return Q_ERR(ENOSYS);

    // first download queued, so we want the mod filelist
    need_list = LIST_EMPTY(&cls.download.queue);

    ret = CL_QueueDownload(path, type);
    if (ret)
        return ret;

    if (!cl_http_filelists->integer)
        return Q_ERR_SUCCESS;

    if (need_list) {
        //grab the filelist
        len = Q_snprintf(temp, sizeof(temp), "%s.filelist", http_gamedir());
        if (len < sizeof(temp))
            CL_QueueDownload(temp, DL_LIST);

        //this is a nasty hack to let the server know what we're doing so admins don't
        //get confused by a ton of people stuck in CNCT state. it's assumed the server
        //is running R1Q2 if we're even able to do http downloading so hopefully this
        //won't spew an error msg.
        if (!download_default_repo)
            CL_ClientCommand("download http\n");
    }

    //special case for map file lists, i really wanted a server-push mechanism for this, but oh well
    len = strlen(path);
    if (len > 4 && !Q_stricmp(path + len - 4, ".bsp")) {
        len = Q_snprintf(temp, sizeof(temp), "%s/%.*s.filelist", http_gamedir(), (int)(len - 4), path);
        if (len < sizeof(temp))
            CL_QueueDownload(temp, DL_LIST);
    }

    return Q_ERR_SUCCESS;
}

// Validate a path supplied by a filelist.
static void check_and_queue_download(char *path)
{
    size_t          len;
    char            *ext;
    dltype_t        type;
    unsigned        flags;
    path_valid_t    valid;

    len = strlen(path);
    if (len >= MAX_QPATH)
        return;

    ext = strrchr(path, '.');
    if (!ext)
        return;

    ext++;
    if (!ext[0])
        return;

    Q_strlwr(ext);

    if (!strcmp(ext, "pak") || !strcmp(ext, "pkz")) {
        Com_Printf("[HTTP] Filelist is requesting a .%s file '%s'\n", ext, path);
        type = DL_PAK;
    } else {
        type = DL_OTHER;
        if (!CL_CheckDownloadExtension(ext)) {
            Com_WPrintf("[HTTP] Illegal file type '%s' in filelist.\n", path);
            return;
        }
    }

    if (path[0] == '@') {
        if (type == DL_PAK) {
            Com_WPrintf("[HTTP] '@' prefix used on a pak file '%s' in filelist.\n", path);
            return;
        }
        flags = FS_PATH_GAME;
        path++;
        len--;
    } else if (type == DL_PAK) {
        //by definition paks are game-local
        flags = FS_PATH_GAME | FS_TYPE_REAL;
    } else {
        flags = 0;
    }

    len = FS_NormalizePath(path);
    valid = FS_ValidatePath(path);

    if (valid == PATH_INVALID ||
        !Q_ispath(path[0]) ||
        !Q_ispath(path[len - 1]) || //valid path implies len > 0
        strstr(path, "..") ||
        (type == DL_OTHER && !strchr(path, '/')) ||
        (type == DL_PAK && strchr(path, '/'))) {
        Com_WPrintf("[HTTP] Illegal path '%s' in filelist.\n", path);
        return;
    }

    if (FS_FileExistsEx(path, flags))
        return;

    if (valid == PATH_MIXED_CASE)
        Q_strlwr(path);

    if (CL_IgnoreDownload(path))
        return;

    CL_QueueDownload(path, type);
}

// A filelist is in memory, scan and validate it and queue up the files.
static void parse_file_list(dlhandle_t *dl)
{
    char    *list;
    char    *p;

    if (!dl->buffer)
        return;

    if (cl_http_filelists->integer) {
        list = dl->buffer;
        while (*list) {
            p = strchr(list, '\n');
            if (p) {
                if (p > list && *(p - 1) == '\r')
                    *(p - 1) = 0;
                *p = 0;
            }

            if (*list)
                check_and_queue_download(list);

            if (!p)
                break;
            list = p + 1;
        }
    }

    free(dl->buffer);
    dl->buffer = NULL;
}

// A pak file just downloaded, let's see if we can remove some stuff from
// the queue which is in the .pak.
static void rescan_queue(void)
{
    dlqueue_t   *q;

    FOR_EACH_DLQ(q) {
        if (q->state == DL_PENDING && q->type < DL_LIST && FS_FileExists(q->path))
            CL_FinishDownload(q);
    }
}

// Fatal HTTP error occurred, remove any special entries from
// queue and fall back to UDP downloading.
static void abort_downloads(void)
{
    dlqueue_t   *q;

    HTTP_CleanupDownloads();

    cls.download.current = NULL;
    cls.download.percent = 0;
    cls.download.position = 0;

    FOR_EACH_DLQ(q) {
        if (q->state != DL_DONE && q->type >= DL_LIST)
            CL_FinishDownload(q);
        else if (q->state == DL_RUNNING)
            q->state = DL_PENDING;
    }

    CL_RequestNextDownload();
    CL_StartNextDownload();
}

// A download finished, find out what it was, whether there were any errors and
// if so, how severe. If none, rename file and other such stuff.
static void process_downloads(void)
{
    dlhandle_t  *dl;
    dlstate_t   state;
    long        response;
    curl_off_t  dlsize, dlspeed;
    char        size[16], speed[16];
    char        temp[MAX_OSPATH];
    bool        fatal_error = false;
    bool        finished = false;
    bool        running = false;
    const char  *err;
    print_type_t level;
    int         i;

    for (i = 0; i < MAX_DLHANDLES; i++) {
        dl = &download_handles[i];
        state = atomic_load(&dl->state);

        if (state == DL_RUNNING) {
            running = true;
            continue;
        }

        if (state != DL_DONE)
            continue;

        //filelist processing is done on read
        if (dl->file) {
            fclose(dl->file);
            dl->file = NULL;
        }

        switch (dl->result) {
            //for some reason curl returns CURLE_OK for a 404...
        case CURLE_HTTP_RETURNED_ERROR:
        case CURLE_OK:
            curl_easy_getinfo(dl->curl, CURLINFO_RESPONSE_CODE, &response);
            if (dl->result == CURLE_OK && response == 200) {
                //success
                break;
            }

            err = http_strerror(response);

            //404 is non-fatal unless accessing default repository
            if (response == 404 && (!download_default_repo || !dl->path[0])) {
                level = PRINT_ALL;
                goto fail1;
            }

            //every other code is treated as fatal
            //not marking download as done since
            //we are falling back to UDP
            level = PRINT_ERROR;
            fatal_error = true;
            goto fail2;

        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_PEER_FAILED_VERIFICATION:
            //connection problems are fatal
            err = curl_easy_strerror(dl->result);
            level = PRINT_ERROR;
            fatal_error = true;
            goto fail2;

        default:
            err = curl_easy_strerror(dl->result);
            level = PRINT_WARNING;
fail1:
            //we mark download as done even if it errored
            //to prevent multiple attempts.
            CL_FinishDownload(dl->queue);
fail2:
            Com_LPrintf(level,
                        "[HTTP] %s [%s] [%d remaining file%s]\n",
                        dl->queue->path, err, cls.download.pending,
                        cls.download.pending == 1 ? "" : "s");
            if (dl->path[0]) {
                remove(dl->path);
                dl->path[0] = 0;
            }
            if (dl->buffer) {
                free(dl->buffer);
                dl->buffer = NULL;
            }
            atomic_store(&dl->state, DL_FREE);
            finished = true;
            continue;
        }

        //mark as done
        CL_FinishDownload(dl->queue);

        //show some stats
        curl_easy_getinfo(dl->curl, CURLINFO_SIZE_DOWNLOAD_T, &dlsize);
        curl_easy_getinfo(dl->curl, CURLINFO_SPEED_DOWNLOAD_T, &dlspeed);
        Com_FormatSizeLong(size, sizeof(size), dlsize);
        Com_FormatSizeLong(speed, sizeof(speed), dlspeed);

        Com_Printf("[HTTP] %s [%s, %s/sec] [%d remaining file%s]\n",
                   dl->queue->path, size, speed, cls.download.pending,
                   cls.download.pending == 1 ? "" : "s");

        if (dl->path[0]) {
            //rename the temp file
            Q_snprintf(temp, sizeof(temp), "%s/%s", fs_gamedir, dl->queue->path);

            if (rename(dl->path, temp))
                Com_EPrintf("[HTTP] Failed to rename '%s' to '%s': %s\n",
                            dl->path, dl->queue->path, strerror(errno));
            dl->path[0] = 0;

            //a pak file is very special...
            if (dl->queue->type == DL_PAK) {
                CL_RestartFilesystem(!*fs_game->string);
                rescan_queue();
            }
        } else if (!fatal_error) {
            parse_file_list(dl);
        }
        atomic_store(&dl->state, DL_FREE);
        finished = true;
    }

    //fatal error occurred, disable HTTP
    if (fatal_error) {
        abort_downloads();
        return;
    }

    if (finished) {
        cls.download.current = NULL;
        cls.download.percent = 0;
        cls.download.position = 0;

        // see if we have more to dl
        CL_RequestNextDownload();
        return;
    }

    if (running) {
        //don't care which download shows as long as something does :)
        pthread_mutex_lock(&progress_mutex);
        cls.download.current = download_current;
        cls.download.percent = download_percent;
        cls.download.position = download_position;
        pthread_mutex_unlock(&progress_mutex);
    }
}

// Find a free download handle to start another queue entry on.
static dlhandle_t *get_free_handle(void)
{
    dlhandle_t  *dl;
    int         i;

    for (i = 0; i < MAX_DLHANDLES; i++) {
        dl = &download_handles[i];
        if (atomic_load(&dl->state) == DL_FREE)
            return dl;
    }

    return NULL;
}

// Start another HTTP download if possible.
static void start_next_download(void)
{
    dlqueue_t   *q;
    bool        started = false;

    if (!cls.download.pending) {
        return;
    }

    // Check if HTTP download was aborted
    if (!curl_multi)
        return;

    //not enough downloads running, queue some more!
    FOR_EACH_DLQ(q) {
        if (q->state == DL_PENDING) {
            dlhandle_t *dl = get_free_handle();
            if (!dl)
                break;
            if (start_download(q, dl))
                started = true;
        }
        if (q->type == DL_PAK && q->state != DL_DONE)
            break;  // hack for pak file single downloading
    }

    if (started)
        curl_multi_wakeup(curl_multi);
}

static void worker_start_downloads(void)
{
    dlhandle_t  *dl;
    int         i;

    for (i = 0; i < MAX_DLHANDLES; i++) {
        dl = &download_handles[i];
        if (atomic_load(&dl->state) == DL_PENDING) {
            curl_multi_add_handle(curl_multi, dl->curl);
            atomic_store(&dl->state, DL_RUNNING);
        }
    }
}

static void worker_finish_downloads(void)
{
    int         msgs_in_queue;
    CURLMsg     *msg;
    dlhandle_t  *dl;
    CURL        *curl;

    do {
        msg = curl_multi_info_read(curl_multi, &msgs_in_queue);
        if (!msg)
            break;

        if (msg->msg != CURLMSG_DONE)
            continue;

        curl = msg->easy_handle;
        curl_easy_getinfo(curl, CURLINFO_PRIVATE, &dl);

        if (atomic_load(&dl->state) == DL_RUNNING) {
            curl_multi_remove_handle(curl_multi, curl);
            dl->result = msg->data.result;
            atomic_store(&dl->state, DL_DONE);
        }
    } while (msgs_in_queue > 0);
}

static void *worker_func(void *arg)
{
    CURLMcode   ret = CURLM_OK;
    int         count;

    while (1) {
        if (atomic_load(&worker_terminate))
            break;

        worker_start_downloads();

        ret = curl_multi_perform(curl_multi, &count);
        if (ret != CURLM_OK)
            break;

        worker_finish_downloads();

        ret = curl_multi_poll(curl_multi, NULL, 0, INT_MAX, NULL);
        if (ret != CURLM_OK)
            break;
    }

    atomic_store(&worker_status, ret);
    return NULL;
}

/*
===============
HTTP_RunDownloads
===============
*/
void HTTP_RunDownloads(void)
{
    CURLMcode ret;

    if (!curl_multi)
        return;

    ret = atomic_load(&worker_status);
    if (ret != CURLM_OK) {
        Com_EPrintf("[HTTP] Error running downloads: %s.\n",
                    curl_multi_strerror(ret));
        abort_downloads();
        return;
    }

    process_downloads();
    start_next_download();
}
