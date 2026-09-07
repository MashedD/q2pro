#!/usr/bin/env python3
"""Generate the FSR3.1.4 Vulkan shader blobs used by Q2Pro.

The upstream SDK normally generates large permutation headers with its
Windows-only FidelityFX shader compiler.  Q2Pro only needs the Vulkan
upscaler, so compile the required permutations directly with glslc and use
SPIR-V debug reflection to emit the SDK's FfxShaderBlob tables.
"""

import argparse
import os
import re
import subprocess
import tempfile


PASSES = (
    ('prepare_inputs', 'ffx_fsr3upscaler_prepare_inputs_pass.glsl', 0, 0),
    ('luma_pyramid', 'ffx_fsr3upscaler_luma_pyramid_pass.glsl', 1, 0),
    ('shading_change_pyramid', 'ffx_fsr3upscaler_shading_change_pyramid_pass.glsl', 2, 0),
    ('shading_change', 'ffx_fsr3upscaler_shading_change_pass.glsl', 3, 0),
    ('prepare_reactivity', 'ffx_fsr3upscaler_prepare_reactivity_pass.glsl', 4, 0),
    ('luma_instability', 'ffx_fsr3upscaler_luma_instability_pass.glsl', 5, 0),
    ('accumulate', 'ffx_fsr3upscaler_accumulate_pass.glsl', 6, 0),
    ('accumulate_sharpen', 'ffx_fsr3upscaler_accumulate_pass.glsl', 7, 1),
    ('rcas', 'ffx_fsr3upscaler_rcas_pass.glsl', 8, 0),
    ('debug_view', 'ffx_fsr3upscaler_debug_view_pass.glsl', 9, 0),
    ('autogen_reactive', 'ffx_fsr3upscaler_autogen_reactive_pass.glsl', 10, 0),
)

INCLUDES = (
    'sdk/include',
    'sdk/include/FidelityFX/gpu',
    'sdk/include/FidelityFX/gpu/fsr3upscaler',
)


def run_checked(command):
    result = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise RuntimeError('%s\n%s' % (' '.join(command), result.stderr))


def reflect(disassembler, spv):
    text = subprocess.run([disassembler, spv], check=True,
                          stdout=subprocess.PIPE, text=True).stdout
    names = {}
    bindings = {}
    sets = {}
    variables = {}
    image_types = {}
    sampler_types = set()
    pointer_types = {}

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
        match = re.search(r'^\s*(%\S+) = OpTypePointer (\S+) (%\S+)$', line)
        if match:
            pointer_types[match.group(1)] = match.group(3)
        match = re.search(r'^\s*(%\S+) = OpVariable (%\S+) (UniformConstant|Uniform)\b', line)
        if match:
            variables[match.group(1)] = (match.group(2), match.group(3))

    resources = {
        'cb': [],
        'srv': [],
        'uav': [],
        'sampler': [],
    }
    for variable, (pointer, storage) in variables.items():
        if variable not in bindings or variable not in sets:
            continue
        pointee = pointer_types.get(pointer)
        name = names.get(variable, '')
        binding = bindings[variable]
        descriptor_set = sets[variable]
        if storage == 'Uniform':
            resources['cb'].append((binding, descriptor_set, name))
        elif pointee in sampler_types:
            resources['sampler'].append((binding, descriptor_set, name))
        elif pointee in image_types:
            if image_types[pointee] == 1:
                resources['srv'].append((binding, descriptor_set, name))
            elif image_types[pointee] == 2:
                resources['uav'].append((binding, descriptor_set, name))

    for values in resources.values():
        values.sort()
    return resources


def identifier(value):
    return re.sub(r'[^A-Za-z0-9_]', '_', value)


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
    out.write('  %s_names, %s_bindings, %s_counts, %s_spaces,\n' %
              (names['cb'], names['cb'], names['cb'], names['cb']))
    out.write('  %s_names, %s_bindings, %s_counts, %s_spaces,\n' %
              (names['srv'], names['srv'], names['srv'], names['srv']))
    out.write('  %s_names, %s_bindings, %s_counts, %s_spaces,\n' %
              (names['uav'], names['uav'], names['uav'], names['uav']))
    out.write('  nullptr, nullptr, nullptr, nullptr,\n')
    out.write('  nullptr, nullptr, nullptr, nullptr,\n')
    out.write('  nullptr, nullptr, nullptr, nullptr,\n')
    out.write('  nullptr, nullptr, nullptr, nullptr,\n')
    out.write('};\n\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-dir', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--glslc', required=True)
    parser.add_argument('--spirv-dis', required=True)
    parser.add_argument('--spirv-opt', required=True)
    args = parser.parse_args()

    source_dir = os.path.abspath(args.source_dir)
    shader_dir = os.path.join(source_dir, 'sdk/src/backends/vk/shaders/fsr3upscaler')
    include_args = []
    for include in INCLUDES:
        include_args += ['-I' + os.path.join(source_dir, include)]

    blobs = {}
    with tempfile.TemporaryDirectory(prefix='q2-fsr3-') as temp:
        for key, filename, pass_id, sharpen in PASSES:
            blobs[key] = {}
            for use_lut in (0, 1):
                for wave64 in (0, 1):
                    for fp16 in (0, 1):
                        label = 'q2_fsr3_%s_lut%d_wave%d_fp%d' % (
                            key, use_lut, wave64, fp16)
                        spv = os.path.join(temp, label + '.spv')
                        command = [args.glslc, '-g', '--target-env=vulkan1.2',
                                   '-fshader-stage=compute', '-Os',
                                   '-DFFX_GPU=1', '-DFFX_GLSL=1',
                                   '-DFFX_HALF=%d' % fp16,
                                   '-DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0',
                                   '-DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0',
                                   '-DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1',
                                   '-DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0',
                                   '-DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2',
                                   '-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE=%d' % use_lut,
                                   '-DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT=0',
                                   '-DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1',
                                   '-DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS=0',
                                   '-DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH=0',
                                   '-DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING=%d' % sharpen]
                        command += include_args + [os.path.join(shader_dir, filename),
                                                   '-o', spv]
                        run_checked(command)
                        with open(spv, 'rb') as stream:
                            data = stream.read()
                        resources = reflect(args.spirv_dis, spv)
                        stripped = os.path.join(temp, label + '.stripped.spv')
                        run_checked([args.spirv_opt, '--strip-debug', spv,
                                     '-o', stripped])
                        with open(stripped, 'rb') as stream:
                            data = stream.read()
                        blobs[key][use_lut, wave64, fp16] = (data, resources)

    output_dir = os.path.dirname(args.output)
    os.makedirs(output_dir, exist_ok=True)
    with open(args.output, 'w', encoding='utf-8') as out:
        out.write('#pragma once\n#include <stdint.h>\n#include <string.h>\n')
        out.write('#if defined(__GNUC__)\n#define Q2_FSR3_UNUSED __attribute__((unused))\n#else\n#define Q2_FSR3_UNUSED\n#endif\n')
        out.write('#include <FidelityFX/host/ffx_fsr3upscaler.h>\n')
        out.write('#include <FidelityFX/host/ffx_types.h>\n\n')
        for key, _, _, _ in PASSES:
            for use_lut in (0, 1):
                for wave64 in (0, 1):
                    for fp16 in (0, 1):
                        label = 'q2_fsr3_%s_lut%d_wave%d_fp%d' % (
                            key, use_lut, wave64, fp16)
                        emit_blob(out, label,
                                  blobs[key][use_lut, wave64, fp16][0],
                                  blobs[key][use_lut, wave64, fp16][1])
        out.write('''
static inline FfxErrorCode fsr3UpscalerGetPermutationBlobByIndex(
    FfxFsr3UpscalerPass pass, uint32_t options, FfxShaderBlob *out)
{
    if (!out)
        return FFX_ERROR_INVALID_POINTER;
    const uint32_t lut = (options >> 0) & 1;
    const uint32_t wave = (options >> 6) & 1;
    const uint32_t fp16 = (options >> 7) & 1;
    switch (pass) {
''')
        for key, _, pass_id, _ in PASSES:
            out.write('    case %d: {\n' % pass_id)
            out.write('        const FfxShaderBlob *blob = &q2_fsr3_%s_lut0_wave0_fp0;\n' % key)
            out.write('        if (lut) blob = &q2_fsr3_%s_lut1_wave0_fp0;\n' % key)
            out.write('        if (wave) blob = lut ? &q2_fsr3_%s_lut1_wave1_fp0 : &q2_fsr3_%s_lut0_wave1_fp0;\n' % (key, key))
            out.write('        if (fp16) blob = lut ? (wave ? &q2_fsr3_%s_lut1_wave1_fp1 : &q2_fsr3_%s_lut1_wave0_fp1) : (wave ? &q2_fsr3_%s_lut0_wave1_fp1 : &q2_fsr3_%s_lut0_wave0_fp1);\n' % (key, key, key, key))
            out.write('        memcpy(out, blob, sizeof(*out));\n')
            out.write('        return FFX_OK;\n    }\n')
        out.write('''    default:
        return FFX_ERROR_INVALID_ENUM;
    }
}

static inline FfxErrorCode fsr3UpscalerIsWave64(uint32_t options, bool &isWave64)
{
    isWave64 = (options & (1u << 6)) != 0;
    return FFX_OK;
}
''')


if __name__ == '__main__':
    main()
