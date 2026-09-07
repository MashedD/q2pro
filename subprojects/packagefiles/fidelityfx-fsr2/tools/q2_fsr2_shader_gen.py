#!/usr/bin/env python3
"""Generate portable FSR2 Vulkan shader blobs for Q2Pro.

FSR2's upstream permutation compiler is distributed as a Windows executable.
The GLSL sources are portable, so Q2Pro compiles the two permutations needed
by its fixed FSR2 configuration with glslc and obtains descriptor reflection
from spirv-dis.
"""

import argparse
import os
import re
import subprocess
import tempfile

PASSES = {
    'tcr_autogen': 'ffx_fsr2_tcr_autogen_pass.glsl',
    'autogen_reactive': 'ffx_fsr2_autogen_reactive_pass.glsl',
    'accumulate': 'ffx_fsr2_accumulate_pass.glsl',
    'compute_luminance_pyramid': 'ffx_fsr2_compute_luminance_pyramid_pass.glsl',
    'depth_clip': 'ffx_fsr2_depth_clip_pass.glsl',
    'lock': 'ffx_fsr2_lock_pass.glsl',
    'reconstruct_previous_depth': 'ffx_fsr2_reconstruct_previous_depth_pass.glsl',
    'rcas': 'ffx_fsr2_rcas_pass.glsl',
}


def run_checked(command):
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.PIPE, text=True)


def reflect(disassembler, spv):
    text = subprocess.run([disassembler, spv], check=True,
                          stdout=subprocess.PIPE, text=True).stdout
    names = {}
    bindings = {}
    sets = {}
    variables = {}
    image_types = {}
    uniform_types = set()
    for line in text.splitlines():
        m = re.search(r'OpName (%\S+) "([^"]+)"', line)
        if m:
            names[m.group(1)] = m.group(2)
        m = re.search(r'OpDecorate (%\S+) Binding (\d+)', line)
        if m:
            bindings[m.group(1)] = int(m.group(2))
        m = re.search(r'OpDecorate (%\S+) DescriptorSet (\d+)', line)
        if m:
            sets[m.group(1)] = int(m.group(2))
        m = re.search(r'(%\S+) = OpTypeImage .* (\d+) (\S+)$', line)
        if m:
            image_types[m.group(1)] = int(m.group(2))
        m = re.search(r'(%\S+) = OpVariable (%\S+) (Uniform|UniformConstant)', line)
        if m:
            variables[m.group(1)] = (m.group(2), m.group(3))

    srvs = []
    uavs = []
    cbs = []
    for var, (pointer, storage) in variables.items():
        if sets.get(var) != 1 or var not in bindings:
            continue
        binding = bindings[var]
        name = names.get(var, '')
        if storage == 'Uniform':
            cbs.append((binding, name))
            continue
        type_id = pointer.rsplit('_', 1)[-1]
        # Pointer ids normally end in the pointee id, but use a second pass
        # fallback for ids containing underscores.
        if type_id not in image_types:
            type_id = pointer.replace('_ptr_UniformConstant_', '%')
        sampled = image_types.get(type_id)
        if sampled == 2:
            uavs.append((binding, name))
        elif sampled == 1:
            srvs.append((binding, name))

    srvs.sort()
    uavs.sort()
    cbs.sort()
    return srvs, uavs, cbs


def c_identifier(name):
    return re.sub(r'[^A-Za-z0-9_]', '_', name)


def emit_blob(out, label, data, srvs, uavs, cbs):
    out.write('static const uint8_t %s_data[] = {\n' % label)
    for i in range(0, len(data), 16):
        out.write('  ' + ', '.join('0x%02x' % x for x in data[i:i + 16]) + ',\n')
    out.write('};\n')
    def emit_meta(suffix, values):
        if values:
            out.write('static const char *%s_%s_names[] = {%s};\n' %
                      (label, suffix, ', '.join('"%s"' % n for _, n in values)))
            out.write('static const uint32_t %s_%s_bindings[] = {%s};\n' %
                      (label, suffix, ', '.join(str(b) for b, _ in values)))
        else:
            out.write('static const char **%s_%s_names = nullptr;\n' % (label, suffix))
            out.write('static const uint32_t *%s_%s_bindings = nullptr;\n' % (label, suffix))
    emit_meta('srv', srvs)
    emit_meta('uav', uavs)
    emit_meta('cb', cbs)
    out.write('static const Fsr2ShaderBlobVK %s = {%s_data, sizeof(%s_data), %d, %d, %d, '
              '%s_uav_names, %s_uav_bindings, %s_srv_names, %s_srv_bindings, '
              '%s_cb_names, %s_cb_bindings};\n' %
              (label, label, label, len(uavs), len(srvs), len(cbs),
               label, label, label, label, label, label))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-dir', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--glslc', required=True)
    parser.add_argument('--spirv-dis', required=True)
    args = parser.parse_args()

    shader_dir = os.path.join(args.source_dir, 'src', 'ffx-fsr2-api', 'shaders')
    output_dir = os.path.dirname(args.output)
    os.makedirs(output_dir, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='q2-fsr2-') as temp:
        blobs = {}
        for key, filename in PASSES.items():
            variants = []
            for lut in (0, 1):
                spv = os.path.join(temp, '%s_%d.spv' % (key, lut))
                shader = os.path.join(shader_dir, filename)
                command = [args.glslc, '--target-env=vulkan1.1', '-fshader-stage=compute',
                           '-g', '-Os', '-DFFX_GPU=1', '-DFFX_GLSL=1',
                           '-DFFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE=%d' % lut,
                           '-DFFX_FSR2_OPTION_HDR_COLOR_INPUT=0',
                           '-DFFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1',
                           '-DFFX_FSR2_OPTION_JITTERED_MOTION_VECTORS=0',
                           '-DFFX_FSR2_OPTION_INVERTED_DEPTH=0',
                           '-DFFX_FSR2_OPTION_APPLY_SHARPENING=0',
                           '-DFFX_HALF=0', '-I' + shader_dir, shader, '-o', spv]
                run_checked(command)
                with open(spv, 'rb') as stream:
                    data = stream.read()
                variants.append((data, reflect(args.spirv_dis, spv)))
            blobs[key] = variants

        with open(args.output, 'w', encoding='utf-8') as out:
            out.write('#pragma once\n#include <stdint.h>\n#include "ffx_fsr2_shaders_vk.h"\n\n')
            for key, variants in blobs.items():
                labels = []
                for lut, (data, metadata) in enumerate(variants):
                    label = 'q2_fsr2_%s_%d' % (c_identifier(key), lut)
                    emit_blob(out, label, data, *metadata)
                    labels.append(label)
                out.write('static const Fsr2ShaderBlobVK %s[2] = {%s, %s};\n\n' %
                          ('q2_fsr2_' + c_identifier(key), labels[0], labels[1]))


if __name__ == '__main__':
    main()
