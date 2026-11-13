/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// The Vulkan renderer's compiled shaders (SPIR-V), embedded as static
// arrays. Every source file that creates shader modules includes this
// header; the compiler keeps only the arrays a file uses.

#pragma once

#if USE_VULKAN

static const uint32_t vk_rect_vert_spv[] =
#include "vk_rect_vert_spv.h"
;

static const uint32_t vk_rect_frag_spv[] =
#include "vk_rect_frag_spv.h"
;

static const uint32_t vk_vignette_vert_spv[] =
#include "vk_vignette_vert_spv.h"
;

static const uint32_t vk_tex_vert_spv[] =
#include "vk_tex_vert_spv.h"
;

static const uint32_t vk_tex_frag_spv[] =
#include "vk_tex_frag_spv.h"
;

static const uint32_t vk_scene_frag_spv[] =
#include "vk_scene_frag_spv.h"
;

static const uint32_t vk_waterwarp_frag_spv[] =
#include "vk_waterwarp_frag_spv.h"
;

static const uint32_t vk_bloom_blur_frag_spv[] =
#include "vk_bloom_blur_frag_spv.h"
;

static const uint32_t vk_bloom_downscale_frag_spv[] =
#include "vk_bloom_downscale_frag_spv.h"
;

static const uint32_t vk_ssr_vert_spv[] =
#include "vk_ssr_vert_spv.h"
;

static const uint32_t vk_ssr_frag_spv[] =
#include "vk_ssr_frag_spv.h"
;

static const uint32_t vk_ssr_resolve_frag_spv[] =
#include "vk_ssr_resolve_frag_spv.h"
;

static const uint32_t vk_color3d_vert_spv[] =
#include "vk_color3d_vert_spv.h"
;

static const uint32_t vk_color3d_frag_spv[] =
#include "vk_color3d_frag_spv.h"
;

static const uint32_t vk_world_vert_spv[] =
#include "vk_world_vert_spv.h"
;

static const uint32_t vk_world_lit_vert_spv[] =
#include "vk_world_lit_vert_spv.h"
;

static const uint32_t vk_world_frag_spv[] =
#include "vk_world_frag_spv.h"
;

static const uint32_t vk_world_lit_frag_spv[] =
#include "vk_world_lit_frag_spv.h"
;

#if USE_VULKAN_RAYTRACING
static const uint32_t vk_rt_ao_bake_vert_spv[] =
#include "vk_rt_ao_bake_vert_spv.h"
;
static const uint32_t vk_rt_ao_bake_frag_spv[] =
#include "vk_rt_ao_bake_frag_spv.h"
;
static const uint32_t vk_world_lit_rt_frag_spv[] =
#include "vk_world_lit_rt_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_frag_spv[] =
#include "vk_world_pixel_rt_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_vert_spv[] =
#include "vk_world_pixel_rt_vert_spv.h"
;
static const uint32_t vk_world_pixel_rt_alpha_frag_spv[] =
#include "vk_world_pixel_rt_alpha_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_glow_frag_spv[] =
#include "vk_world_pixel_rt_glow_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_glow_alpha_frag_spv[] =
#include "vk_world_pixel_rt_glow_alpha_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_quad_frag_spv[] =
#include "vk_world_pixel_rt_quad_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_quad_glow_frag_spv[] =
#include "vk_world_pixel_rt_quad_glow_frag_spv.h"
;
#endif

static const uint32_t vk_world_alpha_frag_spv[] =
#include "vk_world_alpha_frag_spv.h"
;

static const uint32_t vk_world_lit_alpha_frag_spv[] =
#include "vk_world_lit_alpha_frag_spv.h"
;

static const uint32_t vk_world_pixel_vert_spv[] =
#include "vk_world_pixel_vert_spv.h"
;

static const uint32_t vk_world_pixel_frag_spv[] =
#include "vk_world_pixel_frag_spv.h"
;

static const uint32_t vk_world_pixel_alpha_frag_spv[] =
#include "vk_world_pixel_alpha_frag_spv.h"
;
static const uint32_t vk_world_pixel_glow_frag_spv[] =
#include "vk_world_pixel_glow_frag_spv.h"
;
static const uint32_t vk_world_pixel_glow_alpha_frag_spv[] =
#include "vk_world_pixel_glow_alpha_frag_spv.h"
;

static const uint32_t vk_world_glow_frag_spv[] =
#include "vk_world_glow_frag_spv.h"
;

static const uint32_t vk_alias_vert_spv[] =
#include "vk_alias_vert_spv.h"
;

static const uint32_t vk_alias_shadow_vert_spv[] =
#include "vk_alias_shadow_vert_spv.h"
;

static const uint32_t vk_alias_shadow_frag_spv[] =
#include "vk_alias_shadow_frag_spv.h"
;

static const uint32_t vk_alias_frag_spv[] =
#include "vk_alias_frag_spv.h"
;

static const uint32_t vk_alias_alpha_frag_spv[] =
#include "vk_alias_alpha_frag_spv.h"
;

#endif // USE_VULKAN
