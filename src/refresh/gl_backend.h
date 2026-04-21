/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "refresh/refresh.h"

bool GLR_Init(bool total);
void GLR_Shutdown(bool total);
void GLR_BeginRegistration(const char *map);
qhandle_t GLR_RegisterModel(const char *name);
qhandle_t GLR_RegisterImage(const char *name, imagetype_t type,
                            imageflags_t flags);
void GLR_SetSky(const char *name, float rotate, bool autorotate,
                const vec3_t axis);
void GLR_EndRegistration(void);
void GLR_RenderFrame(const refdef_t *fd);
void GLR_LightPoint(const vec3_t origin, vec3_t light);
void GLR_ClearColor(void);
void GLR_SetAlpha(float alpha);
void GLR_SetColor(uint32_t color);
void GLR_SetClipRect(const clipRect_t *clip);
float GLR_ClampScale(cvar_t *var);
void GLR_SetScale(float scale);
void GLR_DrawChar(int x, int y, int flags, int ch, qhandle_t font);
int GLR_DrawString(int x, int y, int flags, size_t max_chars,
                   const char *string, qhandle_t font);
bool GLR_GetPicSize(int *w, int *h, qhandle_t pic);
void GLR_DrawPic(int x, int y, qhandle_t pic);
void GLR_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic);
void GLR_DrawKeepAspectPic(int x, int y, int w, int h, qhandle_t pic);
void GLR_DrawStretchRaw(int x, int y, int w, int h);
void GLR_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic);
void GLR_TileClear(int x, int y, int w, int h, qhandle_t pic);
void GLR_DrawFill8(int x, int y, int w, int h, int c);
void GLR_DrawFill32(int x, int y, int w, int h, uint32_t color);
void GLR_BeginFrame(void);
void GLR_EndFrame(void);
void GLR_ModeChanged(int width, int height, int flags);
bool GLR_VideoSync(void);
r_opengl_config_t GLR_GetGLConfig(void);
