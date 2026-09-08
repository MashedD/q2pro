#!/usr/bin/env python3
"""Compare identical demo timestamps, keeping gameplay settings isolated.

Requires a desktop/Vulkan device. Does not modify the demo or game directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tempfile


SAMPLE = re.compile(r'FSR sample: time=([\d.]+) cpu_us=(\d+) gpu_us=(\d+)')


def summarize(values):
    values = sorted(values)
    return {'mean': statistics.mean(values), 'median': statistics.median(values),
            'p95': values[int((len(values) - 1) * .95)],
            'p99': values[int((len(values) - 1) * .99)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--basedir', type=Path, required=True)
    parser.add_argument('--demo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--gamelib', type=Path,
                        help='optional game library; defaults to a sibling of --binary')
    parser.add_argument('--resolution', default='1280x720')
    parser.add_argument('--quality', choices=['quality', 'balanced', 'performance', 'ultra_performance'], default='quality')
    parser.add_argument('--precision', choices=['auto', 'fp32', 'fp16'], default='auto')
    parser.add_argument('--subgroup', choices=['native', '32', '64'], default='native')
    parser.add_argument('--composition', choices=['0', '1'], default='1')
    parser.add_argument('--raytracing', choices=['0', '1'], default='0')
    parser.add_argument('--frames', type=int, default=90)
    parser.add_argument('--warmup', type=int, default=15)
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    if args.frames < 1 or args.warmup < 0 or args.repeats < 1:
        parser.error('invalid sample counts')
    if not re.fullmatch(r'[1-9]\d*x[1-9]\d*', args.resolution):
        parser.error('resolution must be WIDTHxHEIGHT')
    for path in (args.binary, args.demo):
        if not path.is_file():
            parser.error(f'file not found: {path}')
    gamelib = args.gamelib
    if gamelib is None:
        suffix = 'gamex86_64.so' if args.binary.suffix != '.exe' else 'gamex86.dll'
        candidate = args.binary.resolve().parent / suffix
        if candidate.is_file():
            gamelib = candidate
    if gamelib is not None and not gamelib.is_file():
        parser.error(f'file not found: {gamelib}')
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'demo_sha256': hashlib.sha256(args.demo.read_bytes()).hexdigest(),
              'settings': {key: str(value) for key, value in vars(args).items()}, 'runs': []}
    for repeat in range(args.repeats):
        captured = {}
        # Alternate execution order to reduce systematic thermal/order bias.
        for mode in (['native', 'fsr'] if repeat % 2 == 0 else ['fsr', 'native']):
            with tempfile.TemporaryDirectory(prefix='q2-fsr-benchmark-') as home:
                demos = Path(home) / 'baseq2/demos'
                demos.mkdir(parents=True)
                shutil.copyfile(args.demo, demos / 'benchmark.dm2')
                settings = dict(basedir=str(args.basedir.resolve()), homedir=home,
                    vid_ref='vk', vid_fullscreen='0', vid_geometry=args.resolution,
                    gl_swapinterval='0', vk_present_mode='immediate', cl_async='0',
                    timedemo='1', cl_demowait='1', gl_bloom='1',
                    vk_raytracing=args.raytracing, r_fsr='1' if mode == 'fsr' else '0',
                    r_fsr_auto='0', r_fsr_quality=args.quality, r_fsr_motion='auto',
                    r_fsr_composition_mask=args.composition, r_fsr_mip_bias='auto',
                    r_fsr_sharpness='0', r_fsr_frame_generation='0',
                    vk_fsr_precision=args.precision, vk_fsr_subgroup=args.subgroup,
                    vk_fsr_profile='0', vk_fsr_debug='off', vk_fsr_benchmark='1',
                    con_notifytime='0', logfile='1', logfile_name='benchmark', logfile_flush='2')
                if gamelib is not None:
                    settings['sys_forcegamelib'] = str(gamelib.resolve())
                command = [str(args.binary.resolve())]
                for key, value in settings.items():
                    command += ['+set', key, value]
                command += ['+demo', 'benchmark', '+wait', str(args.frames + args.warmup + 300), '+quit']
                completed = subprocess.run(command, capture_output=True, text=True, timeout=180)
                log_path = Path(home) / 'baseq2/logs/benchmark.log'
                log = log_path.read_text(errors='replace') if log_path.exists() else completed.stdout + completed.stderr
                (args.output / f'{repeat}-{mode}.log').write_text(log)
                if completed.returncode or re.search(r'Vulkan error|FSR3.*failed|spatial-fallback', log):
                    raise RuntimeError(f'{mode} run failed; see saved log')
                samples = {}
                for time, cpu, gpu in SAMPLE.findall(log):
                    samples.setdefault(time, (int(cpu), int(gpu)))
                captured[mode] = samples
                report.setdefault('device', re.findall(r'(?:Using Vulkan device:|Vulkan FSR3 enabled capabilities:).*', log))
        common = sorted(set(captured['native']) & set(captured['fsr']), key=float)
        times = common[args.warmup:args.warmup + args.frames]
        if len(times) != args.frames:
            raise RuntimeError('not enough matching demo timestamps; use a longer demo or fewer frames')
        run = {'timestamps': times}
        for mode in captured:
            cpu, gpu = zip(*(captured[mode][time] for time in times))
            run[mode] = {'cpu_us': summarize(cpu), 'gpu_us': summarize(gpu),
                         'frame_cost_us': summarize([max(c, g) for c, g in zip(cpu, gpu)]),
                         'timing_source': 'CPU/GPU' if all(gpu) else 'CPU-only'}
        report['runs'].append(run)
    (args.output / 'report.json').write_text(json.dumps(report, indent=2))
    print(args.output / 'report.json')


if __name__ == '__main__':
    main()
