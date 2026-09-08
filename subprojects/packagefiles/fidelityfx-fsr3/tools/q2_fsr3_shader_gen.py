#!/usr/bin/env python3
"""Generate Vulkan shader blobs for FSR3 upscaling and frame generation."""

import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile

UPSCALER_PASSES = (
    ('prepare_inputs', 'fsr3upscaler/ffx_fsr3upscaler_prepare_inputs_pass.glsl', 0, 0),
    ('luma_pyramid', 'fsr3upscaler/ffx_fsr3upscaler_luma_pyramid_pass.glsl', 1, 0),
    ('shading_change_pyramid', 'fsr3upscaler/ffx_fsr3upscaler_shading_change_pyramid_pass.glsl', 2, 0),
    ('shading_change', 'fsr3upscaler/ffx_fsr3upscaler_shading_change_pass.glsl', 3, 0),
    ('prepare_reactivity', 'fsr3upscaler/ffx_fsr3upscaler_prepare_reactivity_pass.glsl', 4, 0),
    ('luma_instability', 'fsr3upscaler/ffx_fsr3upscaler_luma_instability_pass.glsl', 5, 0),
    ('accumulate', 'fsr3upscaler/ffx_fsr3upscaler_accumulate_pass.glsl', 6, 0),
    ('accumulate_sharpen', 'fsr3upscaler/ffx_fsr3upscaler_accumulate_pass.glsl', 7, 1),
    ('rcas', 'fsr3upscaler/ffx_fsr3upscaler_rcas_pass.glsl', 8, 0),
    ('debug_view', 'fsr3upscaler/ffx_fsr3upscaler_debug_view_pass.glsl', 9, 0),
    ('autogen_reactive', 'fsr3upscaler/ffx_fsr3upscaler_autogen_reactive_pass.glsl', 10, 0),
)

FRAME_PASSES = (
    ('reconstruct_and_dilate', 'frameinterpolation/ffx_frameinterpolation_reconstruct_and_dilate_pass.glsl', 0),
    ('setup', 'frameinterpolation/ffx_frameinterpolation_setup_pass.glsl', 1),
    ('reconstruct_previous_depth', 'frameinterpolation/ffx_frameinterpolation_reconstruct_previous_depth_pass.glsl', 2),
    ('game_motion_vector_field', 'frameinterpolation/ffx_frameinterpolation_game_motion_vector_field_pass.glsl', 3),
    ('optical_flow_vector_field', 'frameinterpolation/ffx_frameinterpolation_optical_flow_vector_field_pass.glsl', 4),
    ('disocclusion_mask', 'frameinterpolation/ffx_frameinterpolation_disocclusion_mask_pass.glsl', 5),
    ('interpolation', 'frameinterpolation/ffx_frameinterpolation_pass.glsl', 6),
    ('inpainting_pyramid', 'frameinterpolation/ffx_frameinterpolation_compute_inpainting_pyramid_pass.glsl', 7),
    ('inpainting', 'frameinterpolation/ffx_frameinterpolation_inpainting_pass.glsl', 8),
    ('game_vector_field_inpainting_pyramid', 'frameinterpolation/ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass.glsl', 9),
    ('debug_view', 'frameinterpolation/ffx_frameinterpolation_debug_view_pass.glsl', 10),
)

OPTICALFLOW_PASSES = (
    ('prepare_luma', 'opticalflow/ffx_opticalflow_prepare_luma_pass.glsl', 0),
    ('luminance_pyramid', 'opticalflow/ffx_opticalflow_compute_luminance_pyramid_pass.glsl', 1),
    ('scd_histogram', 'opticalflow/ffx_opticalflow_generate_scd_histogram_pass.glsl', 2),
    ('scd_divergence', 'opticalflow/ffx_opticalflow_compute_scd_divergence_pass.glsl', 3),
    ('optical_flow', 'opticalflow/ffx_opticalflow_compute_optical_flow_advanced_pass_v5.glsl', 4),
    ('filter_optical_flow', 'opticalflow/ffx_opticalflow_filter_optical_flow_pass_v5.glsl', 5),
    ('scale_optical_flow', 'opticalflow/ffx_opticalflow_scale_optical_flow_advanced_pass_v5.glsl', 6),
)


def run_checked(command):
    result = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise RuntimeError('%s\n%s' % (' '.join(command), result.stderr))


def reflect(disassembler, spv):
    text = subprocess.run([disassembler, spv], check=True,
                          stdout=subprocess.PIPE, text=True).stdout
    names, bindings, sets, variables = {}, {}, {}, {}
    image_types, sampler_types, pointer_types = {}, set(), {}
    for line in text.splitlines():
        match = re.search(r'OpName (%\S+) "([^"]+)"', line)
        if match:
            names[match.group(1)] = match.group(2)
        match = re.search(r'OpDecorate (%\S+) Binding (\d+)', line)
        if match:
            bindings[match.group(1)] = int(match.group(2))
        match = re.search(r'OpDecorate (%\S+) DescriptorSet (\d+)', line)
        if match:
            sets[match.group(1)] = int(match.group(2))
        match = re.search(r'^\s*(%\S+) = OpTypeImage .* (\d+) \S+$', line)
        if match:
            image_types[match.group(1)] = int(match.group(2))
        match = re.search(r'^\s*(%\S+) = OpTypeSampler$', line)
        if match:
            sampler_types.add(match.group(1))
        match = re.search(r'^\s*(%\S+) = OpTypePointer \S+ (%\S+)$', line)
        if match:
            pointer_types[match.group(1)] = match.group(2)
        match = re.search(r'^\s*(%\S+) = OpVariable (%\S+) (UniformConstant|Uniform)\b', line)
        if match:
            variables[match.group(1)] = (match.group(2), match.group(3))

    resources = {'cb': [], 'srv': [], 'uav': [], 'sampler': []}
    for variable, (pointer, storage) in variables.items():
        if variable not in bindings or variable not in sets:
            continue
        pointee = pointer_types.get(pointer)
        value = (bindings[variable], sets[variable], names.get(variable, ''))
        if storage == 'Uniform':
            resources['cb'].append(value)
        elif pointee in sampler_types:
            resources['sampler'].append(value)
        elif pointee in image_types:
            resources['srv' if image_types[pointee] == 1 else 'uav'].append(value)
    for values in resources.values():
        values.sort()
    return resources


def emit_array(out, label, data):
    out.write('static const uint8_t %s_data[] Q2_FSR3_UNUSED = {\n' % label)
    for offset in range(0, len(data), 16):
        out.write('  %s,\n' % ', '.join('0x%02x' % value
                                         for value in data[offset:offset + 16]))
    out.write('};\n')


def emit_metadata(out, label, values):
    if not values:
        out.write('static const char **%s_names Q2_FSR3_UNUSED = nullptr;\n' % label)
        out.write('static const uint32_t *%s_bindings Q2_FSR3_UNUSED = nullptr;\n' % label)
        out.write('static const uint32_t *%s_counts Q2_FSR3_UNUSED = nullptr;\n' % label)
        out.write('static const uint32_t *%s_spaces Q2_FSR3_UNUSED = nullptr;\n' % label)
        return
    out.write('static const char *%s_names[] Q2_FSR3_UNUSED = {%s};\n' %
              (label, ', '.join('"%s"' % name for _, _, name in values)))
    out.write('static const uint32_t %s_bindings[] Q2_FSR3_UNUSED = {%s};\n' %
              (label, ', '.join(str(binding) for binding, _, _ in values)))
    out.write('static const uint32_t %s_counts[] Q2_FSR3_UNUSED = {%s};\n' %
              (label, ', '.join('1' for _ in values)))
    out.write('static const uint32_t %s_spaces[] Q2_FSR3_UNUSED = {%s};\n' %
              (label, ', '.join(str(space) for _, space, _ in values)))


def emit_blob(out, label, data, resources):
    emit_array(out, label, data)
    names = {}
    for kind in ('cb', 'srv', 'uav', 'sampler'):
        names[kind] = '%s_%s' % (label, kind)
        emit_metadata(out, names[kind], resources[kind])
    out.write('static const FfxShaderBlob %s Q2_FSR3_UNUSED = {\n' % label)
    out.write('  %s_data, sizeof(%s_data),\n' % (label, label))
    out.write('  %d, %d, %d, 0, 0, 0, 0,\n' %
              (len(resources['cb']), len(resources['srv']), len(resources['uav'])))
    for kind in ('cb', 'srv', 'uav'):
        out.write('  %s_names, %s_bindings, %s_counts, %s_spaces,\n' %
                  tuple(names[kind] for _ in range(4)))
    out.write('  nullptr, nullptr, nullptr, nullptr,\n' * 4)
    out.write('};\n\n')


def compile_shader(args, source_dir, temp, relpath, label, defines):
    include_args = []
    for include in ('sdk/include', 'sdk/include/FidelityFX/gpu',
                    'sdk/include/FidelityFX/gpu/fsr3upscaler'):
        include_args += ['-I' + os.path.join(source_dir, include)]
    label = '_'.join(str(part) for part in label) if isinstance(label, tuple) else label
    spv = os.path.join(temp, label + '.spv')
    command = [args.glslc, '-g', '--target-env=vulkan1.2',
               '-fshader-stage=compute', '-O' if args.optimization == 'speed' else '-Os',
               '-DFFX_GPU=1', '-DFFX_GLSL=1']
    command += ['-D%s=%s' % pair for pair in defines]
    command += include_args + [os.path.join(source_dir, 'sdk/src/backends/vk/shaders', relpath), '-o', spv]
    run_checked(command)
    resources = reflect(args.spirv_dis, spv)
    stripped = os.path.join(temp, label + '.stripped.spv')
    run_checked([args.spirv_opt, '--strip-debug', spv, '-o', stripped])
    run_checked([args.spirv_val, '--target-env', 'vulkan1.2', stripped])
    with open(stripped, 'rb') as stream:
        return stream.read(), resources


def add_blob(blobs, key, args, source_dir, temp, relpath, defines):
    blobs[key] = compile_shader(args, source_dir, temp, relpath, key, defines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-dir', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--glslc', required=True)
    parser.add_argument('--spirv-dis', required=True)
    parser.add_argument('--spirv-opt', required=True)
    parser.add_argument('--spirv-val', default='spirv-val')
    parser.add_argument('--optimization', choices=('size', 'speed'), default='size')
    parser.add_argument('--manifest')
    args = parser.parse_args()
    source_dir = os.path.abspath(args.source_dir)
    blobs = {}
    with tempfile.TemporaryDirectory(prefix='q2-fsr3-') as temp:
        for key, path, _, sharpen in UPSCALER_PASSES:
            for lut in (0, 1):
                for wave in (0, 1):
                    for fp16 in (0, 1):
                        label = 'q2_fsr3_up_%s_lut%d_wave%d_fp%d' % (key, lut, wave, fp16)
                        defines = [('FFX_HALF', fp16),
                                   ('FFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF', 0),
                                   ('FFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF', 0),
                                   ('FFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF', 1),
                                   ('FFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF', 0),
                                   ('FFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE', 2),
                                   ('FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE', lut),
                                   ('FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT', 0),
                                   ('FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS', 1),
                                   ('FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS', 0),
                                   ('FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH', 0),
                                   ('FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING', sharpen)]
                        add_blob(blobs, ('up', key, lut, wave, fp16), args, source_dir, temp, path, defines)
        for key, path, _ in FRAME_PASSES:
            for wave in (0, 1):
                for fp16 in (0, 1):
                    label = 'q2_fsr3_frame_%s_wave%d_fp%d' % (key, wave, fp16)
                    defines = [('FFX_HALF', fp16), ('FFX_PREFER_WAVE64', wave),
                               ('FFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS', 1),
                               ('FFX_FRAMEINTERPOLATION_OPTION_JITTER_MOTION_VECTORS', 0),
                               ('FFX_FRAMEINTERPOLATION_OPTION_JITTERED_MOTION_VECTORS', 0),
                               ('FFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH', 0)]
                    add_blob(blobs, ('frame', key, wave, fp16), args, source_dir, temp, path, defines)
        for key, path, _ in OPTICALFLOW_PASSES:
            for wave in (0, 1):
                for fp16 in (0, 1):
                    label = 'q2_fsr3_of_%s_wave%d_fp%d' % (key, wave, fp16)
                    defines = [('FFX_HALF', fp16), ('FFX_PREFER_WAVE64', wave),
                               ('FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT', 0)]
                    add_blob(blobs, ('of', key, wave, fp16), args, source_dir, temp, path, defines)

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, 'w', encoding='utf-8') as out:
        out.write('#pragma once\n#include <stdint.h>\n#include <string.h>\n')
        out.write('#if defined(__GNUC__)\n#define Q2_FSR3_UNUSED __attribute__((unused))\n#else\n#define Q2_FSR3_UNUSED\n#endif\n')
        out.write('#include <FidelityFX/host/ffx_fsr3upscaler.h>\n#include <FidelityFX/host/ffx_frameinterpolation.h>\n#include <FidelityFX/host/ffx_opticalflow.h>\n#include <FidelityFX/host/ffx_types.h>\n\n')
        canonical = {}
        manifest = []
        for key, value in blobs.items():
            if key[0] == 'up':
                label = 'q2_fsr3_up_%s_lut%d_wave%d_fp%d' % key[1:]
            elif key[0] == 'frame':
                label = 'q2_fsr3_frame_%s_wave%d_fp%d' % key[1:]
            else:
                label = 'q2_fsr3_of_%s_wave%d_fp%d' % key[1:]
            digest = hashlib.sha256(value[0]).hexdigest()
            identity = (digest, json.dumps(value[1], sort_keys=True))
            if identity in canonical:
                out.write('#define %s %s\n' % (label, canonical[identity]))
            else:
                canonical[identity] = label
                emit_blob(out, label, value[0], value[1])
            manifest.append(dict(label=label, canonical=canonical[identity],
                                 sha256=digest, bytes=len(value[0]), resources=value[1]))

        out.write('''static inline FfxErrorCode fsr3UpscalerGetPermutationBlobByIndex(FfxFsr3UpscalerPass pass, uint32_t options, FfxShaderBlob *out) {
  if (!out) return FFX_ERROR_INVALID_POINTER;
  const uint32_t lut = (options >> 0) & 1, wave = (options >> 6) & 1, fp = (options >> 7) & 1;
  switch (pass) {
''')
        for name, _, pass_id, _ in UPSCALER_PASSES:
            out.write('    case %d: { const FfxShaderBlob *b = &q2_fsr3_up_%s_lut0_wave0_fp0;\n' % (pass_id, name))
            out.write('      if (lut) b = &q2_fsr3_up_%s_lut1_wave0_fp0;\n' % name)
            out.write('      if (wave) b = lut ? &q2_fsr3_up_%s_lut1_wave1_fp0 : &q2_fsr3_up_%s_lut0_wave1_fp0;\n' % (name, name))
            out.write('      if (fp) b = lut ? (wave ? &q2_fsr3_up_%s_lut1_wave1_fp1 : &q2_fsr3_up_%s_lut1_wave0_fp1) : (wave ? &q2_fsr3_up_%s_lut0_wave1_fp1 : &q2_fsr3_up_%s_lut0_wave0_fp1);\n' % (name, name, name, name))
            out.write('      memcpy(out, b, sizeof(*out)); return FFX_OK; }\n')
        out.write('    default: return FFX_ERROR_INVALID_ENUM; } }\n\n')
        out.write('static inline FfxErrorCode fsr3UpscalerIsWave64(uint32_t options, bool &wave) { wave = (options & (1u << 6)) != 0; return FFX_OK; }\n\n')
        out.write('''static inline FfxErrorCode frameInterpolationGetPermutationBlobByIndex(FfxFrameInterpolationPass pass, FfxBindStage stage, uint32_t options, FfxShaderBlob *out) {
  (void)stage; if (!out) return FFX_ERROR_INVALID_POINTER;
  const uint32_t wave = (options >> 3) & 1, fp = (options >> 4) & 1;
  switch (pass) {
''')
        for name, _, pass_id in FRAME_PASSES:
            out.write('    case %d: { const FfxShaderBlob *b = &q2_fsr3_frame_%s_wave0_fp0; if (wave) b = fp ? &q2_fsr3_frame_%s_wave1_fp1 : &q2_fsr3_frame_%s_wave1_fp0; else if (fp) b = &q2_fsr3_frame_%s_wave0_fp1; memcpy(out, b, sizeof(*out)); return FFX_OK; }\n' % (pass_id, name, name, name, name))
        out.write('    default: return FFX_ERROR_INVALID_ENUM; } }\n\n')
        out.write('static inline FfxErrorCode frameInterpolationIsWave64(uint32_t options, bool &wave) { wave = (options & (1u << 3)) != 0; return FFX_OK; }\n\n')
        out.write('''static inline FfxErrorCode opticalflowGetPermutationBlobByIndex(FfxOpticalflowPass pass, uint32_t options, FfxShaderBlob *out) {
  if (!out) return FFX_ERROR_INVALID_POINTER;
  const uint32_t wave = (options >> 0) & 1, fp = (options >> 1) & 1;
  switch (pass) {
''')
        for name, _, pass_id in OPTICALFLOW_PASSES:
            out.write('    case %d: { const FfxShaderBlob *b = &q2_fsr3_of_%s_wave0_fp0; if (wave) b = fp ? &q2_fsr3_of_%s_wave1_fp1 : &q2_fsr3_of_%s_wave1_fp0; else if (fp) b = &q2_fsr3_of_%s_wave0_fp1; memcpy(out, b, sizeof(*out)); return FFX_OK; }\n' % (pass_id, name, name, name, name))
        out.write('    default: return FFX_ERROR_INVALID_ENUM; } }\n\n')
        out.write('static inline FfxErrorCode opticalflowIsWave64(uint32_t options, bool &wave) { wave = (options & 1u) != 0; return FFX_OK; }\n')


    if args.manifest:
        with open(args.manifest, 'w', encoding='utf-8') as output:
            json.dump(dict(optimization=args.optimization, permutations=manifest), output, indent=2)


if __name__ == '__main__':
    main()
