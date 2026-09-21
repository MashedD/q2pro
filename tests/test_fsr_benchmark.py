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

    def test_jitter_fields_are_typed_and_reported(self):
        parsed = fsr_benchmark.parse_benchmark_log(
            'VK FSR3 frame: id=20 jitter_x=0.125 jitter_y=-0.25 '
            'jitter_phase=3 jitter_phases=8 jitter_ready=yes '
            'reset_reason=camera_cut\n'
            'VK FSR3 frame: id=21 jitter_ready=no reset_reason=pause\n')

        first, second = parsed['jitter']
        self.assertEqual(first['frame_id'], 20)
        self.assertAlmostEqual(first['jitter_x'], 0.125)
        self.assertAlmostEqual(first['jitter_y'], -0.25)
        self.assertEqual(first['jitter_phase'], 3)
        self.assertEqual(first['jitter_phases'], 8)
        self.assertIs(first['jitter_ready'], True)
        self.assertEqual(first['reset_reason'], 'camera_cut')
        self.assertIs(second['jitter_ready'], False)
        self.assertEqual(second['reset_reason'], 'pause')
        self.assertEqual(parsed['frame_records'][0], first)

    def test_optional_diagnostics_records_are_typed_and_frame_scoped(self):
        parsed = fsr_benchmark.parse_benchmark_log(
            'VK FSR3 diagnostics: frame=20 motion_pixels=100 '
            'motion_nonfinite=2 reactive_coverage=0.18 enabled=yes\n'
            'VK FSR3 diagnostics: frame=21 status=unavailable\n')

        first, second = parsed['diagnostics']
        self.assertEqual(first['frame_id'], 20)
        self.assertEqual(first['motion_pixels'], 100)
        self.assertEqual(first['motion_nonfinite'], 2)
        self.assertAlmostEqual(first['reactive_coverage'], 0.18)
        self.assertIs(first['enabled'], True)
        self.assertEqual(second, {'frame_id': 21, 'status': 'unavailable'})

    def test_missing_new_telemetry_remains_backward_compatible(self):
        parsed = fsr_benchmark.parse_benchmark_log(
            'VK FSR3 frame: id=30 reset=no pause=no\n')

        self.assertEqual(parsed['jitter'], [])
        self.assertEqual(parsed['diagnostics'], [])
        self.assertEqual(parsed['dynamic'], [])
        self.assertEqual(parsed['frame_records'][0]['frame_id'], 30)

    def test_dynamic_resolution_telemetry_is_typed_and_optional(self):
        parsed = fsr_benchmark.parse_benchmark_log(
            'VK FSR3 dynamic: enabled=yes source=GPU target_ms=16.67 '
            'measured_ms=19.25 current_scale=1.500 '
            'recommended_scale=1.625 hysteresis_ms=0.75 cooldown=45 '
            'samples=0 applied=no\n')

        self.assertEqual(len(parsed['dynamic']), 1)
        record = parsed['dynamic'][0]
        self.assertIs(record['enabled'], True)
        self.assertEqual(record['source'], 'GPU')
        self.assertAlmostEqual(record['target_ms'], 16.67)
        self.assertAlmostEqual(record['recommended_scale'], 1.625)
        self.assertEqual(record['cooldown'], 45)
        self.assertIs(record['applied'], False)

    def test_dynamic_resolution_transaction_fields_are_typed(self):
        parsed = fsr_benchmark.parse_benchmark_log(
            'VK FSR3 dynamic: enabled=yes source=GPU target_ms=16.67 '
            'measured_ms=19.25 current_scale=1.625 '
            'recommended_scale=1.625 applied=yes transaction=12\n')

        record = parsed['dynamic'][0]
        self.assertIs(record['applied'], True)
        self.assertEqual(record['transaction'], 12)

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
