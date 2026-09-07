project('fidelityfx-fsr3', 'cpp', version: '3.1.4')

fsr3_root = meson.current_source_dir()
fsr3_include = include_directories(
  'sdk/include',
  'sdk/src/shared',
  'sdk/src/backends/shared',
  'sdk/src/backends/shared/blob_accessors',
  'sdk/src/components',
)

python = find_program('python3')
glslc = find_program('glslc')
spirv_dis = find_program('spirv-dis')
spirv_opt = find_program('spirv-opt')

vk_backend_source = custom_target(
  'q2_fsr3_vk_backend',
  input: 'sdk/src/backends/vk/ffx_vk.cpp',
  output: 'q2_fsr3_vk_backend.cpp',
  command: [python, files('tools/q2_fsr3_vk_compat.py'), '@INPUT@', '@OUTPUT@'],
)

shader_header = custom_target('q2_fsr3_shader_blobs',
  input: files(
    'sdk/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_accumulate_pass.glsl',
    'sdk/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_autogen_reactive_pass.glsl',
    'sdk/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_debug_view_pass.glsl',
    'sdk/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_luma_instability_pass.glsl',
    'sdk/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_luma_pyramid_pass.glsl',
    'sdk/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_prepare_inputs_pass.glsl',
    'sdk/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_prepare_reactivity_pass.glsl',
    'sdk/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_rcas_pass.glsl',
    'sdk/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_shading_change_pass.glsl',
    'sdk/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_shading_change_pyramid_pass.glsl',
  ),
  output: 'q2_fsr3_shader_blobs.h',
  command: [python, files('tools/q2_fsr3_shader_gen.py'),
            '--source-dir', fsr3_root,
            '--output', '@OUTPUT@',
            '--glslc', glslc,
            '--spirv-dis', spirv_dis,
            '--spirv-opt', spirv_opt],
)

fsr3_sources = files(
  'sdk/src/shared/ffx_assert.cpp',
  'sdk/src/shared/ffx_breadcrumbs_list.cpp',
  'src/shared/ffx_message.cpp',
  'sdk/src/shared/ffx_object_management.cpp',
  'sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp',
  'src/backends/shared/ffx_shader_blobs.cpp',
  'src/backends/vk/ffx_vk_stubs.cpp',
) + [vk_backend_source, shader_header]

fsr3_dep = declare_dependency(
  include_directories: fsr3_include,
)
