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
bool VKR_Init(bool total);
void VKR_Shutdown(bool total);
void VKR_ModeChanged(int width, int height, int flags);
#endif
