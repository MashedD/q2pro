#!/usr/bin/env python3
"""Compile a Vulkan GLSL shader into a C initializer of 32-bit SPIR-V words.

Usage: vk_shader.py COMPILER INPUT OUTPUT

COMPILER is glslc (from shaderc) or glslangValidator (from glslang). Both
produce the same SPIR-V; the output is "{0x07230203,...}" as glslc -mfmt=c
writes it, which vk_backend.c includes as an array initializer. glslang is
supported because some distributions, such as Ubuntu 22.04, don't package
glslc.
"""

import os
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 4:
        sys.exit('usage: vk_shader.py COMPILER INPUT OUTPUT')
    compiler, source, output = sys.argv[1:]

    if 'glslc' in os.path.basename(compiler).lower():
        subprocess.run([compiler, '-mfmt=c', source, '-o', output], check=True)
        return

    with tempfile.TemporaryDirectory() as tmp:
        spv = os.path.join(tmp, 'shader.spv')
        # glslangValidator picks the stage from the file extension.
        subprocess.run([compiler, '-V', source, '-o', spv],
                       check=True, stdout=subprocess.DEVNULL)
        with open(spv, 'rb') as f:
            code = f.read()

    if len(code) % 4:
        sys.exit(f'{source}: SPIR-V size {len(code)} is not a multiple of 4')
    words = [int.from_bytes(code[i:i + 4], 'little')
             for i in range(0, len(code), 4)]
    lines = [','.join(f'0x{w:08x}' for w in words[i:i + 4])
             for i in range(0, len(words), 4)]
    with open(output, 'w') as f:
        f.write('{' + ',\n'.join(lines) + '}\n')


if __name__ == '__main__':
    main()
