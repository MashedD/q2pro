project('fidelityfx-fsr2', 'cpp', version: '2.2.1')

fsr2_include = include_directories(
  '.',
  'src/ffx-fsr2-api',
  'src/ffx-fsr2-api/vk',
  'src/ffx-fsr2-api/vk/shaders',
)

python = find_program('python3')
vk_backend_source = custom_target(
  'q2_fsr2_vk_backend',
  input: meson.current_source_dir() / 'src/ffx-fsr2-api/vk/ffx_fsr2_vk.cpp',
  output: 'q2_fsr2_vk_backend.cpp',
  command: [python, files('tools/q2_fsr2_vk_compat.py'), '@INPUT@', '@OUTPUT@'],
)

glslc = find_program('glslc')
spirv_dis = find_program('spirv-dis')
shader_sources = files(
  'src/ffx-fsr2-api/shaders/ffx_fsr2_tcr_autogen_pass.glsl',
  'src/ffx-fsr2-api/shaders/ffx_fsr2_autogen_reactive_pass.glsl',
  'src/ffx-fsr2-api/shaders/ffx_fsr2_accumulate_pass.glsl',
  'src/ffx-fsr2-api/shaders/ffx_fsr2_compute_luminance_pyramid_pass.glsl',
  'src/ffx-fsr2-api/shaders/ffx_fsr2_depth_clip_pass.glsl',
  'src/ffx-fsr2-api/shaders/ffx_fsr2_lock_pass.glsl',
  'src/ffx-fsr2-api/shaders/ffx_fsr2_reconstruct_previous_depth_pass.glsl',
  'src/ffx-fsr2-api/shaders/ffx_fsr2_rcas_pass.glsl',
)
shader_header = custom_target('q2_fsr2_shader_blobs',
  input: shader_sources,
  output: 'q2_fsr2_shader_blobs.h',
  command: [python, files('tools/q2_fsr2_shader_gen.py'),
            '--source-dir', meson.current_source_dir(),
            '--output', '@OUTPUT@',
            '--glslc', glslc,
            '--spirv-dis', spirv_dis],
)

fsr2_sources = files(
  'src/ffx-fsr2-api/ffx_assert.cpp',
  'src/ffx-fsr2-api/ffx_fsr2.cpp',
  'src/ffx-fsr2-api/vk/shaders/ffx_fsr2_shaders_vk.cpp',
) + [vk_backend_source, shader_header]

fsr2_dep = declare_dependency(
  include_directories: fsr2_include,
)
