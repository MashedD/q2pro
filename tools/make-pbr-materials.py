#!/usr/bin/env python3
"""Generate material maps for Vulkan ray tracing from wall textures.

For every texture under textures/ in the given packages or directories, this
writes textures/<path>_n.png next to it in a new package: RGB is a tangent
space normal map (X right, Y down the texture, as the texture is laid out;
OpenGL convention, 128 is flat) and alpha shifts roughness, where 128 keeps
the value from materials/rt_reflections.mat, lower is smoother and higher
is rougher.

The maps are derived from the color only: relief comes from brightness with
broad shading removed, and roughness from how bright and how busy a spot is.
That is a guess, not hand-made relief, but computed once with care it is
smoother than what a shader can guess per pixel.

Later inputs override earlier ones, as packages do in the game. Generated
maps are made from the textures they are given, so share them only where
the textures' own license allows.

Usage:
    make-pbr-materials.py -o out.pkz texturepack.pkz [more.pkz or dir ...]
"""

import argparse
import io
import os
import sys
import zipfile

import numpy as np
from PIL import Image

EXTENSIONS = ('.png', '.tga', '.jpg', '.jpeg', '.wal')
SKIP_PARTS = ('sky', '/env/')


def collect(inputs):
    """Returns {texture name without extension: (source, member)}."""
    found = {}
    for src in inputs:
        if os.path.isdir(src):
            for root, _, files in os.walk(src):
                for f in files:
                    path = os.path.join(root, f)
                    rel = os.path.relpath(path, src).replace(os.sep, '/')
                    add(found, rel, (src, path))
        else:
            with zipfile.ZipFile(src) as z:
                for member in z.namelist():
                    add(found, member, (src, member))
    return found


def add(found, rel, where):
    lower = rel.lower()
    if not lower.startswith('textures/') or not lower.endswith(EXTENSIONS):
        return
    if lower.endswith('.wal'):
        return      # paletted originals: needs the palette, not handled
    stem = rel.rsplit('.', 1)[0]
    if stem.endswith(('_n', '_glow', '_light')) or any(p in lower for p in SKIP_PARTS):
        return
    found[stem] = where


def load(where, cache):
    src, member = where
    if os.path.isdir(src):
        return Image.open(member)
    z = cache.get(src)
    if z is None:
        z = cache[src] = zipfile.ZipFile(src)
    return Image.open(io.BytesIO(z.read(member)))


def blur(a, radius):
    """Box blur, applied three times (close to a Gaussian), wrapping around
    the edges because wall textures tile."""
    r = max(1, int(radius))
    for _ in range(3):
        for axis in (0, 1):
            n = a.shape[axis]
            k = min(r, n // 2)
            # running sum over a wrapped copy: O(size) whatever the radius
            padded = np.concatenate([a.take(range(n - k - 1, n), axis=axis), a,
                                     a.take(range(0, k), axis=axis)], axis=axis)
            c = np.cumsum(padded, axis=axis, dtype=np.float64)
            hi = c.take(range(2 * k + 1, 2 * k + 1 + n), axis=axis)
            lo = c.take(range(0, n), axis=axis)
            a = ((hi - lo) / (2 * k + 1)).astype(np.float32)
    return a


def make_maps(img, strength):
    rgb = np.asarray(img.convert('RGB'), dtype=np.float32) / 255.0
    h, w, _ = rgb.shape
    luma = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    size = min(w, h)

    # Height: brightness with broad shading taken out, so painted light and
    # shadow do not turn into slopes, plus a little of the finest detail.
    broad = blur(luma, size / 8)
    height = (luma - broad) + 0.35 * (luma - blur(luma, max(1, size / 64)))
    height = blur(height, max(1, size / 256))

    # Slopes in brightness per texel. Scale them so that the steepest tenth
    # of the texture tilts by about 35 degrees, but never by more than a
    # texture of strong contrast would get, so nearly flat textures don't
    # have their noise blown up into relief.
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    p90 = float(np.percentile(np.hypot(dx, dy), 90)) or 1.0
    scale = min(0.7 / p90, 60.0 * size / 256.0) * strength
    dx *= scale
    dy *= scale
    n = np.stack([-dx, -dy, np.ones_like(dx)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)

    # Roughness shift: busy or dark spots rougher, smooth bright ones
    # smoother.
    busy = blur(np.abs(luma - blur(luma, max(1, size / 32))), max(1, size / 64))
    busy = busy / (float(np.percentile(busy, 95)) or 1.0)
    shift = 0.6 * (np.clip(busy, 0, 1) - 0.5) + 0.4 * (0.5 - np.clip(luma / 0.6, 0, 1))
    alpha = np.clip(128 + shift * 110, 1, 255)

    out = np.empty((h, w, 4), dtype=np.uint8)
    out[..., :3] = np.clip(np.rint((n * 0.5 + 0.5) * 255), 0, 255)
    out[..., 3] = np.rint(alpha)
    return Image.fromarray(out, 'RGBA')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('-o', '--output', required=True, help='package to write')
    ap.add_argument('-s', '--strength', type=float, default=1.0,
                    help='relief strength, default 1')
    ap.add_argument('--max-size', type=int, default=512,
                    help='largest map side in pixels, default 512')
    ap.add_argument('inputs', nargs='+', help='texture packages or directories')
    args = ap.parse_args()

    textures = collect(args.inputs)
    if not textures:
        sys.exit('no textures found under textures/')

    cache = {}
    tmp = args.output + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as out:
        for i, (stem, where) in enumerate(sorted(textures.items()), 1):
            try:
                img = load(where, cache)
                img.load()
            except Exception as e:
                print(f'skipping {stem}: {e}', file=sys.stderr)
                continue
            if max(img.size) > args.max_size:
                f = args.max_size / max(img.size)
                img = img.resize((max(1, round(img.width * f)),
                                  max(1, round(img.height * f))), Image.LANCZOS)
            maps = make_maps(img, args.strength)
            buf = io.BytesIO()
            maps.save(buf, 'PNG', optimize=True)
            out.writestr(stem + '_n.png', buf.getvalue())
            if i % 100 == 0:
                print(f'{i}/{len(textures)}', file=sys.stderr)
    os.replace(tmp, args.output)
    print(f'wrote {len(textures)} maps to {args.output}')


if __name__ == '__main__':
    main()
