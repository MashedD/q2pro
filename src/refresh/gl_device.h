#pragma once

// Sets up the GPU for the OpenGL renderer before it starts, from the
// device cvar, and fills the device list for the video menu. Called with
// opengl false before other renderers start, to undo what it changed.
void GL_SelectDevice(bool opengl, cvar_t *device, cvar_t *device_list);
