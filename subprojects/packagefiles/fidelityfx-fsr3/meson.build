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
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_reconstruct_and_dilate_pass.glsl',
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_setup_pass.glsl',
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_reconstruct_previous_depth_pass.glsl',
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_game_motion_vector_field_pass.glsl',
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_optical_flow_vector_field_pass.glsl',
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_disocclusion_mask_pass.glsl',
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_pass.glsl',
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_compute_inpainting_pyramid_pass.glsl',
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_inpainting_pass.glsl',
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass.glsl',
    'sdk/src/backends/vk/shaders/frameinterpolation/ffx_frameinterpolation_debug_view_pass.glsl',
    'sdk/src/backends/vk/shaders/opticalflow/ffx_opticalflow_prepare_luma_pass.glsl',
    'sdk/src/backends/vk/shaders/opticalflow/ffx_opticalflow_compute_luminance_pyramid_pass.glsl',
    'sdk/src/backends/vk/shaders/opticalflow/ffx_opticalflow_generate_scd_histogram_pass.glsl',
    'sdk/src/backends/vk/shaders/opticalflow/ffx_opticalflow_compute_scd_divergence_pass.glsl',
    'sdk/src/backends/vk/shaders/opticalflow/ffx_opticalflow_compute_optical_flow_advanced_pass_v5.glsl',
    'sdk/src/backends/vk/shaders/opticalflow/ffx_opticalflow_filter_optical_flow_pass_v5.glsl',
    'sdk/src/backends/vk/shaders/opticalflow/ffx_opticalflow_scale_optical_flow_advanced_pass_v5.glsl',
  ),
  output: 'q2_fsr3_shader_blobs.h',
  command: [python, fsr3_root / '../packagefiles/fidelityfx-fsr3/tools/q2_fsr3_shader_gen.py',
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
  'sdk/src/components/fsr3/ffx_fsr3.cpp',
  'sdk/src/components/frameinterpolation/ffx_frameinterpolation.cpp',
  'sdk/src/components/opticalflow/ffx_opticalflow.cpp',
  'src/backends/shared/ffx_shader_blobs.cpp',
  'src/backends/vk/ffx_vk_stubs.cpp',
) + [vk_backend_source, shader_header]

fsr3_dep = declare_dependency(
  include_directories: fsr3_include,
)
