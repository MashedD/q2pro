// GPU selection for the OpenGL renderer. Mesa picks the device for OpenGL
// from the DRI_PRIME environment variable when the driver loads, so the
// renderer sets it from vk_device before creating its context. The video
// subsystem is shut down on every renderer restart, so a new value takes
// effect on the next restart. Only Linux with Mesa is supported; elsewhere
// the GPU list stays "automatic" while OpenGL runs.

#include "shared/shared.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/zone.h"
#include "gl_device.h"

#ifdef __linux__

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_GL_GPUS     8

typedef struct {
    char        address[32];    // PCI address, such as 0000:03:00.0
    unsigned    vendor;
    unsigned    device;
    char        name[128];
} gl_gpu_t;

static bool     dri_prime_saved;
static char     *dri_prime_original;    // NULL when it was not set

static bool read_hex_file(const char *dir, const char *file, unsigned *value)
{
    char path[MAX_OSPATH], text[32];

    if (Q_snprintf(path, sizeof(path), "%s/%s", dir, file) >= sizeof(path))
        return false;

    FILE *fp = fopen(path, "r");
    if (!fp)
        return false;
    bool ok = fgets(text, sizeof(text), fp) &&
        sscanf(text, "%x", value) == 1;
    fclose(fp);
    return ok;
}

static int find_gpus(gl_gpu_t *gpus)
{
    DIR *dir = opendir("/sys/bus/pci/devices");
    if (!dir)
        return 0;

    int count = 0;
    struct dirent *ent;
    while (count < MAX_GL_GPUS && (ent = readdir(dir))) {
        char path[MAX_OSPATH];
        unsigned pci_class;

        if (ent->d_name[0] == '.')
            continue;
        if (Q_snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s",
                       ent->d_name) >= sizeof(path))
            continue;
        // Display controllers have base class 0x03.
        if (!read_hex_file(path, "class", &pci_class) ||
            (pci_class >> 16) != 0x03)
            continue;

        gl_gpu_t *gpu = &gpus[count];
        if (!read_hex_file(path, "vendor", &gpu->vendor) ||
            !read_hex_file(path, "device", &gpu->device))
            continue;
        Q_strlcpy(gpu->address, ent->d_name, sizeof(gpu->address));
        Q_snprintf(gpu->name, sizeof(gpu->name), "GPU %04x:%04x",
                   gpu->vendor, gpu->device);
        count++;
    }
    closedir(dir);
    return count;
}

// Names the GPUs from the PCI ID database, when it is installed.
static void name_gpus(gl_gpu_t *gpus, int count)
{
    static const char *const paths[] = {
        "/usr/share/hwdata/pci.ids",
        "/usr/share/misc/pci.ids",
        "/usr/share/pci.ids",
    };
    FILE *fp = NULL;

    for (int i = 0; i < q_countof(paths) && !fp; i++)
        fp = fopen(paths[i], "r");
    if (!fp)
        return;

    char line[256];
    unsigned vendor = 0;
    while (fgets(line, sizeof(line), fp)) {
        unsigned id;

        if (line[0] == '#' || line[0] == '\n')
            continue;
        // The device classes come after all vendors.
        if (line[0] == 'C' && line[1] == ' ')
            break;
        if (line[0] != '\t') {
            vendor = sscanf(line, "%4x", &id) == 1 ? id : 0;
            continue;
        }
        if (line[1] == '\t' || !vendor || sscanf(line + 1, "%4x", &id) != 1)
            continue;

        for (int i = 0; i < count; i++) {
            if (gpus[i].vendor != vendor || gpus[i].device != id)
                continue;
            char *name = line + 1 + 4;
            while (*name == ' ')
                name++;
            name[strcspn(name, "\r\n")] = 0;
            for (char *p = name; *p; p++) {
                if (*p == '"')
                    *p = '\'';
            }
            Q_strlcpy(gpus[i].name, name, sizeof(gpus[i].name));
        }
    }
    fclose(fp);
}

static void restore_dri_prime(void)
{
    if (dri_prime_original)
        setenv("DRI_PRIME", dri_prime_original, 1);
    else
        unsetenv("DRI_PRIME");
}

static const gl_gpu_t *find_requested(const gl_gpu_t *gpus, int count,
                                      const char *selector)
{
    unsigned vendor, device;
    char extra;

    if (sscanf(selector, "%x:%x%c", &vendor, &device, &extra) == 2) {
        for (int i = 0; i < count; i++)
            if (gpus[i].vendor == vendor && gpus[i].device == device)
                return &gpus[i];
        return NULL;
    }

    for (int i = 0; i < count; i++)
        if (Q_stristr(gpus[i].name, selector))
            return &gpus[i];
    return NULL;
}

void GL_SelectDevice(bool opengl, cvar_t *device, cvar_t *device_list)
{
    // Keep the variable the game was started with, for "automatic" and
    // for the Vulkan renderer, which reads it too.
    if (!dri_prime_saved) {
        const char *value = getenv("DRI_PRIME");
        dri_prime_original = value ? Z_CopyString(value) : NULL;
        dri_prime_saved = true;
    }

    restore_dri_prime();
    if (!opengl)
        return;

    gl_gpu_t gpus[MAX_GL_GPUS];
    int count = find_gpus(gpus);
    name_gpus(gpus, count);

    char list[MAX_STRING_CHARS];
    Q_strlcpy(list, "\"automatic\" \"\"", sizeof(list));
    for (int i = 0; i < count; i++)
        Q_strlcat(list, va(" \"%s\" \"%04x:%04x\"", gpus[i].name,
                           gpus[i].vendor, gpus[i].device), sizeof(list));
    Cvar_SetByVar(device_list, list, FROM_CODE);

    const char *selector = device->string;
    if (!selector[0])
        return;

    const gl_gpu_t *gpu = find_requested(gpus, count, selector);
    if (!gpu) {
        Com_WPrintf("OpenGL device '%s' is not available; using the "
                    "default GPU\n", selector);
        return;
    }

    // Mesa takes the PCI address with underscores, such as
    // pci-0000_03_00_0.
    char value[48];
    Q_snprintf(value, sizeof(value), "pci-%s", gpu->address);
    for (char *p = value + 4; *p; p++) {
        if (*p == ':' || *p == '.')
            *p = '_';
    }
    setenv("DRI_PRIME", value, 1);
    Com_Printf("OpenGL device: %s (DRI_PRIME=%s)\n", gpu->name, value);
}

#else

void GL_SelectDevice(bool opengl, cvar_t *device, cvar_t *device_list)
{
    (void)opengl;
    (void)device;
    (void)device_list;
}

#endif
