/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "refresh/refresh.h"

#if USE_VULKAN
struct screenshot_s;

bool VKR_Init(bool total);
void VKR_Shutdown(bool total);
void VKR_BeginRegistration(const char *map);
qhandle_t VKR_RegisterModel(const char *name);
qhandle_t VKR_RegisterImage(const char *name, imagetype_t type,
                            imageflags_t flags);
void VKR_SetSky(const char *name, float rotate, bool autorotate,
                const vec3_t axis);
void VKR_EndRegistration(void);
void VKR_RenderFrame(const refdef_t *fd);
void VKR_LightPoint(const vec3_t origin, vec3_t light);
void VKR_ClearColor(void);
void VKR_SetAlpha(float alpha);
void VKR_SetColor(uint32_t color);
void VKR_SetClipRect(const clipRect_t *clip);
float VKR_ClampScale(cvar_t *var);
void VKR_SetScale(float scale);
void VKR_DrawChar(int x, int y, int flags, int ch, qhandle_t font);
int VKR_DrawString(int x, int y, int flags, size_t max_chars,
                   const char *string, qhandle_t font);
bool VKR_GetPicSize(int *w, int *h, qhandle_t pic);
void VKR_DrawPic(int x, int y, qhandle_t pic);
void VKR_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic);
void VKR_DrawKeepAspectPic(int x, int y, int w, int h, qhandle_t pic);
void VKR_DrawStretchRaw(int x, int y, int w, int h);
void VKR_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic);
void VKR_TileClear(int x, int y, int w, int h, qhandle_t pic);
void VKR_DrawFill8(int x, int y, int w, int h, int c);
void VKR_DrawFill32(int x, int y, int w, int h, uint32_t color);
void VKR_BeginFrame(void);
void VKR_EndFrame(void);
void VKR_ModeChanged(int width, int height, int flags);
bool VKR_VideoSync(void);
int VKR_ReadPixels(struct screenshot_s *s);
r_opengl_config_t VKR_GetGLConfig(void);
#endif
