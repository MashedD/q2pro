#!/usr/bin/env python3
"""GPU-free regression tests for the FSR benchmark telemetry parser."""

import pathlib
import sys
import unittest


TESTS_DIR = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(TESTS_DIR))
import fsr_benchmark  # noqa: E402


class FsrBenchmarkParserTests(unittest.TestCase):
    def test_frame_results_do_not_leak_between_frames(self):
        log = """
FSR sample: frame=10 sdk_id=0 sdk_valid=yes time=1.000 wall_usec=1000000 cpu_us=100 gpu_us=80
VK FSR3 frame: id=10 reset=yes pause=no reuse=no
VK FSR3 result: frame=10 result=fsr3
FSR sample: frame=11 sdk_id=1 sdk_valid=yes time=1.016 wall_usec=1016000 cpu_us=110 gpu_us=90
VK FSR3 frame: id=11 reset=no pause=no reuse=no
VK FSR3 result: frame=11 result=framegen
FSR sample: frame=12 sdk_id=2 sdk_valid=yes time=1.032 wall_usec=1032000 cpu_us=120 gpu_us=100
VK FSR3 frame: id=12 reset=yes pause=no reuse=no
VK FSR3 result: frame=12 result=spatial-fallback
"""

        parsed = fsr_benchmark.parse_benchmark_log(log)

        self.assertEqual(
            [frame['result'] for frame in parsed['frames']],
            ['fsr3', 'framegen', 'spatial-fallback'],
        )
        self.assertEqual(
            [record['result'] for record in parsed['frame_records']],
            ['fsr3', 'framegen', 'spatial-fallback'],
        )
        self.assertEqual(parsed['result'], 'spatial-fallback')

    def test_missing_frame_result_does_not_inherit_later_result(self):
        parsed = fsr_benchmark.parse_benchmark_log(
            'FSR sample: frame=10 time=1.000 cpu_us=100 gpu_us=80\n'
            'FSR sample: frame=11 time=1.016 cpu_us=110 gpu_us=90\n'
            'VK FSR3 result: frame=11 result=framegen\n')

        self.assertNotIn('result', parsed['frames'][0])
        self.assertEqual(parsed['frames'][1]['result'], 'framegen')

    def test_reset_pause_and_reuse_are_booleans(self):
        parsed = fsr_benchmark.parse_benchmark_log(
            'VK FSR3 frame: id=20 reset=yes pause=no reuse=true result=fsr3\n'
            'VK FSR3 frame: id=21 reset=false pause=yes reuse=no result=fsr3\n')

        first, second = parsed['frame_records']
        self.assertIs(first['reset'], True)
        self.assertIs(first['paused'], False)
        self.assertIs(first['reuse'], True)
        self.assertIs(second['reset'], False)
        self.assertIs(second['paused'], True)
        self.assertIs(second['reuse'], False)

    def test_legacy_samples_without_frame_ids_remain_supported(self):
        parsed = fsr_benchmark.parse_benchmark_log(
            'FSR sample: time=2.000 cpu_us=200 gpu_us=150\n'
            'Vulkan FSR3 presentation: result=fsr3\n')

        self.assertEqual(list(parsed['samples']), ['2.000'])
        self.assertEqual(parsed['samples']['2.000'][3], None)
        self.assertEqual(parsed['frames'][0]['result'], 'fsr3')

    def test_gpu_timings_are_filtered_by_selected_frame_ids(self):
        parsed = fsr_benchmark.parse_benchmark_log(
            'VK FSR3 GPU: frame=9 sdk_id=9 sdk_valid=yes time=0.900 upscale=90 framegen=30\n'
            'VK FSR3 GPU: frame=10 sdk_id=10 sdk_valid=yes time=1.000 upscale=100 framegen=40\n'
            'VK FSR3 GPU: frame=11 sdk_id=11 sdk_valid=yes time=1.016 upscale=110 framegen=50\n')

        summary = fsr_benchmark.summarize_gpu_timings(
            parsed['gpu_timings'], {10, 11})

        self.assertEqual(summary['upscale_us']['count'], 2)
        self.assertEqual(summary['upscale_us']['mean'], 105)
        self.assertEqual(summary['framegen_us']['min'], 40)
        self.assertEqual(summary['framegen_us']['max'], 50)
        self.assertNotIn('sdk_id_us', summary)
        self.assertNotIn('sdk_valid_us', summary)

    def test_renderer_completion_pacing_requires_wall_timestamps(self):
        samples = {
            '1.000': (100, 80, 1000000, 1),
            '1.016': (100, 80, 1016000, 2),
            '1.032': (100, 80, 1032000, 3),
        }
        pacing = fsr_benchmark.pacing_from_samples(
            samples, ['1.000', '1.016', '1.032'])
        self.assertEqual(pacing['source'], 'renderer-completion')
        self.assertEqual(pacing['count'], 2)
        self.assertAlmostEqual(pacing['fps'], 62.5)

        samples['1.016'] = (100, 80, None, 2)
        unavailable = fsr_benchmark.pacing_from_samples(
            samples, ['1.000', '1.016', '1.032'])
        self.assertEqual(
            unavailable,
            {'count': 0, 'source': 'renderer-completion-unavailable'},
        )

    def test_malformed_and_unknown_telemetry_is_ignored_safely(self):
        log = """
VK FSR3 frame: id=30 reset=maybe pause=yes unknown_field=hello
VK FSR3 result: frame=30 result=fsr3 unexpected-token
VK FSR3 GPU: frame=30 upscale=42 malformed=not-a-number
VK FSR3 frame: this-is-not-key-value telemetry
"""

        parsed = fsr_benchmark.parse_benchmark_log(log)

        self.assertEqual(len(parsed['frame_records']), 2)
        self.assertEqual(parsed['frame_records'][0]['reset'], 'maybe')
        self.assertEqual(parsed['frame_records'][0]['paused'], True)
        self.assertEqual(parsed['frame_records'][0]['unknown_field'], 'hello')
        self.assertEqual(parsed['frame_results'][0]['result'], 'fsr3')
        self.assertEqual(parsed['gpu_timings'][0]['upscale_us'], 42)
        self.assertEqual(parsed['gpu_timings'][0]['malformed_us'], 'not-a-number')


if __name__ == '__main__':
    unittest.main()
