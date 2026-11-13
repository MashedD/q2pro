/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan 2D drawing: characters, strings, pics, fills and the 2D draw state.

#include "vk_local.h"

#if USE_VULKAN

static void vk_draw_texture_rect(int x, int y, int w, int h,
                                 float s1, float t1, float s2, float t2,
                                 qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    unsigned index = image->texnum;
    const vk_texture_t *texture = vk_texture_for_index(index, true);
    if (!texture)
        return;

    vk_draw_texture_resource(x, y, w, h, s1, t1, s2, t2, texture);
}

void VKR_ClearColor(void)
{
    vk.color.u32 = U32_WHITE;
    vk.alt_color.u32 = U32_WHITE;
    vk.color_set = false;
}

void VKR_SetAlpha(float alpha)
{
    byte a = Q_clipf(alpha, 0.0f, 1.0f) * 255;
    if (!vk.color_set) {
        vk.color.u32 = U32_WHITE;
        vk.alt_color.u32 = U32_WHITE;
        vk.color_set = true;
    }
    vk.color.u8[3] = a;
    vk.alt_color.u8[3] = a;
}

void VKR_SetColor(uint32_t color)
{
    vk.color.u32 = color;
    vk.alt_color.u32 = U32_WHITE;
    vk.alt_color.u8[3] = vk.color.u8[3];
    vk.color_set = true;
}

void VKR_SetClipRect(const clipRect_t *clip)
{
    if (clip) {
        vk.clip.left = max(clip->left, 0);
        vk.clip.top = max(clip->top, 0);
        vk.clip.right = min(clip->right, vk_2d_width());
        vk.clip.bottom = min(clip->bottom, vk_2d_height());
        if (vk.clip.right < vk.clip.left ||
            vk.clip.bottom < vk.clip.top) {
            vk.clip_set = false;
            return;
        }
        vk.clip_set = true;
    } else {
        vk.clip_set = false;
    }
}

float VKR_ClampScale(cvar_t *var)
{
    if (!var)
        return 1.0f;

    if (var->value)
        return 1.0f / Cvar_ClampValue(var, 1.0f, 10.0f);

    int scale = 1;

    if (r_config.height < r_config.width) {
        if (r_config.height >= 2160)
            scale = 4;
        else if (r_config.height >= 1080)
            scale = 2;
    } else {
        if (r_config.width >= 3840)
            scale = 4;
        else if (r_config.width >= 1920)
            scale = 2;
    }

    if (vid && vid->get_dpi_scale)
        scale = max(scale, vid->get_dpi_scale());

    return 1.0f / scale;
}

void VKR_SetScale(float scale)
{
    vk.scale = scale;
}

static int vk_font_shadow(void)
{
    return vk_fontshadow ? Q_clip(vk_fontshadow->integer, 0, 2) : 0;
}

void VKR_DrawChar(int x, int y, int flags, int ch, qhandle_t font)
{
    int old_2d = c.batchesDrawn2D;
    int shadow = vk_font_shadow();

    if ((ch & 127) == 32)
        return;

    if (shadow > 0)
        flags |= UI_DROPSHADOW;

    if (flags & UI_ALTCOLOR)
        ch |= 0x80;
    if (flags & UI_XORCOLOR)
        ch ^= 0x80;

    float s = (ch & 15) * 0.0625f;
    float t = ((ch & 255) >> 4) * 0.0625f;
    color_t saved = vk.color;
    bool saved_set = vk.color_set;
    color_t draw_color = (ch & 0x80) ?
        vk.alt_color : (vk.color_set ? vk.color : (color_t){ .u32 = U32_WHITE });

    if ((flags & UI_DROPSHADOW) && ch != 0x83) {
        byte alpha = draw_color.u8[3];

        vk.color.u32 = MakeColor(0, 0, 0, alpha);
        vk.color_set = true;
        vk_draw_texture_rect(x + 1, y + 1, CONCHAR_WIDTH, CONCHAR_HEIGHT,
                             s, t, s + 0.0625f, t + 0.0625f, font);
        if (shadow > 1) {
            vk_draw_texture_rect(x + 2, y + 2, CONCHAR_WIDTH, CONCHAR_HEIGHT,
                                 s, t, s + 0.0625f, t + 0.0625f, font);
        }

        vk.color = saved;
        vk.color_set = saved_set;
    }

    vk.color = draw_color;
    vk.color_set = true;
    vk_draw_texture_rect(x, y, CONCHAR_WIDTH, CONCHAR_HEIGHT,
                         s, t, s + 0.0625f, t + 0.0625f, font);
    vk.color = saved;
    vk.color_set = saved_set;
    c.charsDrawn2D += c.batchesDrawn2D - old_2d;
}

int VKR_DrawString(int x, int y, int flags, size_t max_chars,
                   const char *string, qhandle_t font)
{
    while (max_chars-- && *string) {
        VKR_DrawChar(x, y, flags, (byte)*string++, font);
        x += CONCHAR_WIDTH;
    }

    return x;
}

bool VKR_GetPicSize(int *w, int *h, qhandle_t pic)
{
    return IMG_GetPicSize(w, h, pic);
}

void VKR_DrawPic(int x, int y, qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    vk_draw_texture_rect(x, y, image->width, image->height,
                         image->sl, image->tl, image->sh, image->th, pic);
}

void VKR_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    vk_draw_texture_rect(x, y, w, h,
                         image->sl, image->tl, image->sh, image->th, pic);
}

void VKR_DrawKeepAspectPic(int x, int y, int w, int h, qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    if (image->flags & IF_SCRAP) {
        VKR_DrawStretchPic(x, y, w, h, pic);
        return;
    }

    float scale_w = w;
    float scale_h = h * image->aspect;
    float scale = max(scale_w, scale_h);
    float s = (1.0f - scale_w / scale) * 0.5f;
    float t = (1.0f - scale_h / scale) * 0.5f;

    vk_draw_texture_rect(x, y, w, h, s, t, 1.0f - s, 1.0f - t, pic);
}

void VKR_DrawStretchRaw(int x, int y, int w, int h)
{
    color_t saved = vk.color;
    bool saved_set = vk.color_set;

    vk.color.u32 = U32_WHITE;
    vk.color_set = true;
    vk_draw_texture_resource(x, y, w, h, 0, 0, 1, 1, &vk.raw_texture);
    vk.color = saved;
    vk.color_set = saved_set;
}

void VKR_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic)
{
    if (pic_w <= 0 || pic_h <= 0 || !pic)
        return;

    if (!vk_upload_texture_data(&vk.raw_texture, pic_w, pic_h, pic, false)) {
        Com_WPrintf("Couldn't upload Vulkan raw texture: %s\n",
                    Com_GetLastError());
    } else {
        vk_update_texture_descriptor_with_sampler(&vk.raw_texture,
                                                  vk_sampler_for_pic_flags(IF_NONE));
    }
}

void VKR_TileClear(int x, int y, int w, int h, qhandle_t pic)
{
    const float div64 = 1.0f / 64.0f;
    color_t saved = vk.color;
    bool saved_set = vk.color_set;

    vk.color.u32 = U32_WHITE;
    vk.color_set = true;
    vk_draw_texture_rect(x, y, w, h, x * div64, y * div64,
                         (x + w) * div64, (y + h) * div64, pic);
    vk.color = saved;
    vk.color_set = saved_set;
}

void VKR_DrawFill8(int x, int y, int w, int h, int c)
{
    vk_clear_rect(x, y, w, h, d_8to24table[c & 0xff]);
}

void VKR_DrawFill32(int x, int y, int w, int h, uint32_t color)
{
    vk_clear_rect(x, y, w, h, color);
}

#endif // USE_VULKAN
