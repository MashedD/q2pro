/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/shared.h"
#include "common/common.h"
#include "refresh/refresh.h"
#include "gl_backend.h"

typedef struct refresh_backend_s {
    const char *name;
    ref_video_api_t video_api;

    bool (*init)(bool total);
    void (*shutdown)(bool total);

    void (*begin_registration)(const char *map);
    qhandle_t (*register_model)(const char *name);
    qhandle_t (*register_image)(const char *name, imagetype_t type,
                                imageflags_t flags);
    void (*set_sky)(const char *name, float rotate, bool autorotate,
                    const vec3_t axis);
    void (*end_registration)(void);

    void (*render_frame)(const refdef_t *fd);
    void (*light_point)(const vec3_t origin, vec3_t light);

    void (*clear_color)(void);
    void (*set_alpha)(float alpha);
    void (*set_color)(uint32_t color);
    void (*set_clip_rect)(const clipRect_t *clip);
    float (*clamp_scale)(cvar_t *var);
    void (*set_scale)(float scale);
    void (*draw_char)(int x, int y, int flags, int ch, qhandle_t font);
    int (*draw_string)(int x, int y, int flags, size_t max_chars,
                       const char *string, qhandle_t font);
    bool (*get_pic_size)(int *w, int *h, qhandle_t pic);
    void (*draw_pic)(int x, int y, qhandle_t pic);
    void (*draw_stretch_pic)(int x, int y, int w, int h, qhandle_t pic);
    void (*draw_keep_aspect_pic)(int x, int y, int w, int h, qhandle_t pic);
    void (*draw_stretch_raw)(int x, int y, int w, int h);
    void (*update_raw_pic)(int pic_w, int pic_h, const uint32_t *pic);
    void (*tile_clear)(int x, int y, int w, int h, qhandle_t pic);
    void (*draw_fill8)(int x, int y, int w, int h, int c);
    void (*draw_fill32)(int x, int y, int w, int h, uint32_t color);

    void (*begin_frame)(void);
    void (*end_frame)(void);
    void (*mode_changed)(int width, int height, int flags);
    bool (*video_sync)(void);

    r_opengl_config_t (*get_gl_config)(void);
} refresh_backend_t;

static const refresh_backend_t gl_backend = {
    .name = "gl",
    .video_api = REF_VIDEO_OPENGL,

    .init = GLR_Init,
    .shutdown = GLR_Shutdown,
    .begin_registration = GLR_BeginRegistration,
    .register_model = GLR_RegisterModel,
    .register_image = GLR_RegisterImage,
    .set_sky = GLR_SetSky,
    .end_registration = GLR_EndRegistration,
    .render_frame = GLR_RenderFrame,
    .light_point = GLR_LightPoint,
    .clear_color = GLR_ClearColor,
    .set_alpha = GLR_SetAlpha,
    .set_color = GLR_SetColor,
    .set_clip_rect = GLR_SetClipRect,
    .clamp_scale = GLR_ClampScale,
    .set_scale = GLR_SetScale,
    .draw_char = GLR_DrawChar,
    .draw_string = GLR_DrawString,
    .get_pic_size = GLR_GetPicSize,
    .draw_pic = GLR_DrawPic,
    .draw_stretch_pic = GLR_DrawStretchPic,
    .draw_keep_aspect_pic = GLR_DrawKeepAspectPic,
    .draw_stretch_raw = GLR_DrawStretchRaw,
    .update_raw_pic = GLR_UpdateRawPic,
    .tile_clear = GLR_TileClear,
    .draw_fill8 = GLR_DrawFill8,
    .draw_fill32 = GLR_DrawFill32,
    .begin_frame = GLR_BeginFrame,
    .end_frame = GLR_EndFrame,
    .mode_changed = GLR_ModeChanged,
    .video_sync = GLR_VideoSync,
    .get_gl_config = GLR_GetGLConfig,
};

static const refresh_backend_t *const backends[] = {
    &gl_backend,
    NULL
};

static const refresh_backend_t *backend;

static const refresh_backend_t *active_backend(void)
{
    if (backend)
        return backend;

    return &gl_backend;
}

static const refresh_backend_t *find_backend(const char *name)
{
    for (int i = 0; backends[i]; i++) {
        if (!Q_stricmp(backends[i]->name, name))
            return backends[i];
    }

    return NULL;
}

bool R_Init(bool total)
{
    cvar_t *vid_ref = Cvar_Get("vid_ref", "gl", CVAR_ROM);

    backend = find_backend(vid_ref->string);
    if (!backend) {
        Com_Printf("No such renderer: %s, falling back to gl.\n", vid_ref->string);
        backend = &gl_backend;
    }

    return backend->init(total);
}

void R_Shutdown(bool total)
{
    if (!backend)
        return;

    backend->shutdown(total);

    if (total)
        backend = NULL;
}

void R_BeginRegistration(const char *map)
{
    active_backend()->begin_registration(map);
}

qhandle_t R_RegisterModel(const char *name)
{
    return active_backend()->register_model(name);
}

qhandle_t R_RegisterImage(const char *name, imagetype_t type,
                          imageflags_t flags)
{
    return active_backend()->register_image(name, type, flags);
}

void R_SetSky(const char *name, float rotate, bool autorotate,
              const vec3_t axis)
{
    active_backend()->set_sky(name, rotate, autorotate, axis);
}

void R_EndRegistration(void)
{
    active_backend()->end_registration();
}

void R_RenderFrame(const refdef_t *fd)
{
    active_backend()->render_frame(fd);
}

void R_LightPoint(const vec3_t origin, vec3_t light)
{
    active_backend()->light_point(origin, light);
}

void R_ClearColor(void)
{
    active_backend()->clear_color();
}

void R_SetAlpha(float alpha)
{
    active_backend()->set_alpha(alpha);
}

void R_SetColor(uint32_t color)
{
    active_backend()->set_color(color);
}

void R_SetClipRect(const clipRect_t *clip)
{
    active_backend()->set_clip_rect(clip);
}

float R_ClampScale(cvar_t *var)
{
    return active_backend()->clamp_scale(var);
}

void R_SetScale(float scale)
{
    active_backend()->set_scale(scale);
}

void R_DrawChar(int x, int y, int flags, int ch, qhandle_t font)
{
    active_backend()->draw_char(x, y, flags, ch, font);
}

int R_DrawString(int x, int y, int flags, size_t max_chars,
                 const char *string, qhandle_t font)
{
    return active_backend()->draw_string(x, y, flags, max_chars, string, font);
}

bool R_GetPicSize(int *w, int *h, qhandle_t pic)
{
    return active_backend()->get_pic_size(w, h, pic);
}

void R_DrawPic(int x, int y, qhandle_t pic)
{
    active_backend()->draw_pic(x, y, pic);
}

void R_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic)
{
    active_backend()->draw_stretch_pic(x, y, w, h, pic);
}

void R_DrawKeepAspectPic(int x, int y, int w, int h, qhandle_t pic)
{
    active_backend()->draw_keep_aspect_pic(x, y, w, h, pic);
}

void R_DrawStretchRaw(int x, int y, int w, int h)
{
    active_backend()->draw_stretch_raw(x, y, w, h);
}

void R_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic)
{
    active_backend()->update_raw_pic(pic_w, pic_h, pic);
}

void R_TileClear(int x, int y, int w, int h, qhandle_t pic)
{
    active_backend()->tile_clear(x, y, w, h, pic);
}

void R_DrawFill8(int x, int y, int w, int h, int c)
{
    active_backend()->draw_fill8(x, y, w, h, c);
}

void R_DrawFill32(int x, int y, int w, int h, uint32_t color)
{
    active_backend()->draw_fill32(x, y, w, h, color);
}

void R_BeginFrame(void)
{
    active_backend()->begin_frame();
}

void R_EndFrame(void)
{
    active_backend()->end_frame();
}

void R_ModeChanged(int width, int height, int flags)
{
    active_backend()->mode_changed(width, height, flags);
}

bool R_VideoSync(void)
{
    return active_backend()->video_sync();
}

ref_video_api_t R_GetVideoAPI(void)
{
    if (backend)
        return backend->video_api;

    return REF_VIDEO_OPENGL;
}

r_opengl_config_t R_GetGLConfig(void)
{
    if (backend)
        return backend->get_gl_config();

    return GLR_GetGLConfig();
}
