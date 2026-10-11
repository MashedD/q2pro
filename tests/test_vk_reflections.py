"""CPU math/contract checks for the half-resolution RT reflection pass.

These checks do not replace Vulkan runtime, image-quality, or FPS validation.
Run with: python3 tests/test_vk_reflections.py
"""
import math
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReflectionTests(unittest.TestCase):
    def test_projection_round_trip_with_subviewport(self):
        near, far = 2.0, 8192.0
        p00, p11 = 1.2, -1.7
        p22, p32 = -far / (far - near), -far * near / (far - near)
        for rect in ((0, 0, 1, 1), (0.1, 0.2, 0.7, 0.6)):
            for p in ((20, 30, -200), (-70, -100, -600), (0, 0, -16)):
                x, y, z = p
                ndc = (p00 * x / -z, p11 * y / -z)
                uv = (rect[0] + (ndc[0] + 1) * rect[2] / 2,
                      rect[1] + (ndc[1] + 1) * rect[3] / 2)
                depth = (p22 * z + p32) / -z
                rz = -p32 / min(depth + p22, -0.00001)
                rn = ((uv[0] - rect[0]) / rect[2] * 2 - 1,
                      (uv[1] - rect[1]) / rect[3] * 2 - 1)
                reconstructed = (-rn[0] * rz / p00, -rn[1] * rz / p11, rz)
                for actual, expected in zip(reconstructed, p):
                    self.assertAlmostEqual(actual, expected, places=8)

    def test_plane_check_preserves_slope_and_rejects_step(self):
        # View-depth difference is not a discontinuity on a tilted floor.
        n = (0, math.sqrt(0.5), math.sqrt(0.5))
        delta_on_plane = (20, 60, -60)
        self.assertAlmostEqual(sum(a * b for a, b in zip(n, delta_on_plane)), 0)
        delta_step = tuple(d + 16 * a for d, a in zip(delta_on_plane, n))
        error = abs(sum(a * b for a, b in zip(n, delta_step)))
        self.assertGreater(error, max(0.75, 500 * 0.002) * 3)

    def test_misses_keep_filter_weight(self):
        weights = [math.exp(-0.5 * (x*x + y*y))
                   for y in range(-2, 3) for x in range(-2, 3)]
        result = weights[12] / sum(weights)  # One bright hit, 24 black misses.
        self.assertGreater(result, 0)
        self.assertLess(result, 0.2)

    def test_dim_contribution_encoding(self):
        for rgb in ((0.001, 0.002, 0.003), (0.1, 0.02, 0.03), (0.9, 0.1, 0.02)):
            scale = min(1, max(0.25, max(rgb)))
            encoded = [round(v / scale * 255) / 255 for v in rgb]
            alpha = round(scale * 255) / 255
            for v, actual in zip(rgb, encoded):
                self.assertLess(abs(v - actual * alpha), 0.004)
        # Ordinary UNORM storage erases this; scaled storage preserves it.
        self.assertEqual(round(0.001 * 255), 0)
        self.assertGreater(round(0.001 / 0.25 * 255), 0)

    def test_shader_contracts(self):
        trace = (ROOT / 'src/refresh/vk_rt_reflection_trace.frag').read_text()
        filt = (ROOT / 'src/refresh/vk_rt_reflection_filter.frag').read_text()
        world = (ROOT / 'src/refresh/vk_world_pixel_rt.frag').read_text()
        self.assertIn('gl_RayFlagsCullBackFacingTrianglesEXT', trace)
        self.assertIn('0x03, origin', trace)
        self.assertNotIn('TerminateOnFirstHit', trace)
        self.assertIn('surface_info.z != 0u', trace)
        self.assertIn('if (!deferred_reflections', world)
        self.assertIn('dot(n, qp - p)', filt)
        self.assertIn('dot(n, qn)', filt)
        self.assertIn('sum / weight_sum', filt)
        self.assertNotIn('reflection.a <=', filt)


if __name__ == '__main__':
    unittest.main()
