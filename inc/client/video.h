/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#pragma once

#if USE_VULKAN
#include <vulkan/vulkan.h>
#endif

typedef struct {
    const char *name;

    bool (*probe)(void);
    bool (*init)(void);
    void (*shutdown)(void);
    void (*fatal_shutdown)(void);
    void (*pump_events)(void);

    char *(*get_mode_list)(void);
    int (*get_dpi_scale)(void);
    void (*set_mode)(void);
    void (*update_gamma)(const byte *table);

    void *(*get_proc_addr)(const char *sym);
    void (*swap_buffers)(void);
    void (*swap_interval)(int val);

#if USE_VULKAN
    bool (*get_vk_instance_extensions)(uint32_t *count, const char **names,
                                       uint32_t max_names);
    bool (*create_vk_surface)(VkInstance instance, VkSurfaceKHR *surface);
#endif

    char *(*get_selection_data)(void);
    char *(*get_clipboard_data)(void);
    void (*set_clipboard_data)(const char *data);

    bool (*init_mouse)(void);
    void (*shutdown_mouse)(void);
    void (*grab_mouse)(bool grab);
    void (*warp_mouse)(int x, int y);
    bool (*get_mouse_motion)(int *dx, int *dy);
} vid_driver_t;

extern cvar_t       *vid_geometry;
extern cvar_t       *vid_modelist;
extern cvar_t       *vid_fullscreen;
extern cvar_t       *_vid_fullscreen;

extern const vid_driver_t   *vid;

bool VID_GetFullscreen(vrect_t *rc, int *freq_p, int *depth_p);
bool VID_GetGeometry(vrect_t *rc);
void VID_SetGeometry(const vrect_t *rc);
void VID_ToggleFullscreen(void);
