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


SAMPLE = re.compile(
    r'FSR sample: .*?'
    r'time=(?P<time>[\d.]+)(?: wall_usec=(?P<wall_usec>\d+))? '
    r'cpu_us=(?P<cpu_us>\d+) gpu_us=(?P<gpu_us>\d+)')
PRESENTATION = re.compile(
    r'Vulkan FSR3 presentation: .*?result=(\S+)')
KEY_VALUE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)')
GPU_TIMING = re.compile(r'VK FSR3 GPU: (?P<timings>.*)')
RECORD = re.compile(r'VK FSR3 record: (?P<telemetry>.*)')
FRAME = re.compile(r'VK FSR3 frame: (?P<telemetry>.*)')
FRAME_RESULT = re.compile(r'VK FSR3 result: (?P<telemetry>.*)')
DIAGNOSTICS = re.compile(r'VK FSR3 diagnostics: (?P<telemetry>.*)')
DYNAMIC = re.compile(r'VK FSR3 dynamic: (?P<telemetry>.*)')
DEVICE = re.compile(r'(?:Using Vulkan device:|Vulkan FSR3 enabled capabilities:).*')
FALLBACK = re.compile(
    r'(?:spatial-fallback|resources are incomplete|dispatch failed|'
    r'using spatially upscaled|frame generation disabled|'
    r'preparation failed|configuration failed|unavailable|'
    r'unsupported on this platform|device lost)', re.IGNORECASE)


def fps_from_cost(summary):
    """Return a useful FPS estimate, or None when no timing was collected."""
    mean = summary.get('mean')
    return round(1000000.0 / mean, 3) if mean else None


def pacing_from_timestamps(timestamps, source):
    """Summarize frame pacing and identify the timestamp source."""
    if len(timestamps) < 2:
        return {'count': 0, 'source': source}
    intervals = [(b - a) * 1000000.0
                 for a, b in zip(map(float, timestamps),
                                 map(float, timestamps[1:]))]
    summary = summarize(intervals)
    summary['jitter_us'] = round(summary['stdev'], 3)
    summary['fps'] = fps_from_cost(summary)
    summary['source'] = source
    return summary


def pacing_from_samples(samples, times):
    """Return renderer-completion pacing when completion timestamps exist."""
    wall = [samples[time][2] for time in times]
    if all(value is not None for value in wall):
        return pacing_from_timestamps([value / 1000000.0 for value in wall],
                                      'renderer-completion')
    return {'count': 0, 'source': 'renderer-completion-unavailable'}


def _typed_fields(line):
    """Parse optional key/value telemetry without making it mandatory."""
    fields = {}
    for key, value in KEY_VALUE.findall(line):
        if value.lower() in ('yes', 'true'):
            fields[key] = True
        elif value.lower() in ('no', 'false'):
            fields[key] = False
        else:
            try:
                fields[key] = int(value, 0)
            except ValueError:
                try:
                    fields[key] = float(value)
                except ValueError:
                    match = re.fullmatch(r'(\d+(?:\.\d+)?)us', value)
                    fields[key] = (float(match.group(1)) if '.' in match.group(1)
                                   else int(match.group(1))) if match else value
    return fields


def parse_benchmark_log(log):
    """Parse benchmark telemetry into stable, JSON-friendly structures.

    The optional frame_id/reset/paused/result fields are intentionally parsed
    when present, so this remains compatible with logs from older binaries.
    """
    samples = {}
    frames = []
    gpu_timings = []
    records = []
    frame_records = []
    frame_results = []
    jitter_records = []
    diagnostics = []
    dynamic = []
    presentations = []
    for line in log.splitlines():
        sample = SAMPLE.search(line)
        if sample:
            values = sample.groupdict()
            time = values['time']
            fields = _typed_fields(line)
            entry = {
                'time': time,
                'wall_usec': int(values['wall_usec']) if values['wall_usec'] else None,
                'cpu_us': int(values['cpu_us']),
                'gpu_us': int(values['gpu_us']),
            }
            frame_id = fields.get('frame_id', fields.get('frame'))
            if frame_id is not None:
                entry['frame_id'] = frame_id
            for key in ('result', 'frame_id', 'reset', 'paused'):
                if key in fields:
                    entry[key] = fields[key]
            samples.setdefault(time, (entry['cpu_us'], entry['gpu_us'],
                                      entry['wall_usec'], entry.get('frame_id')))
            frames.append(entry)
        if 'Vulkan FSR3 presentation:' in line:
            fields = _typed_fields(line)
            match = PRESENTATION.search(line)
            if match:
                fields['result'] = match.group(1)
            presentations.append(fields)
        match = RECORD.search(line)
        if match:
            records.append(_typed_fields(match.group('telemetry')))
        match = FRAME.search(line)
        if match:
            fields = _typed_fields(match.group('telemetry'))
            if 'id' in fields:
                fields['frame_id'] = fields.pop('id')
            if 'pause' in fields:
                fields['paused'] = fields.pop('pause')
            frame_records.append(fields)
            records.append(fields)
            if any(key in fields for key in
                   ('jitter_x', 'jitter_y', 'jitter_phase', 'jitter_phases',
                    'jitter_ready', 'reset_reason')):
                jitter_records.append(fields)
        match = FRAME_RESULT.search(line)
        if match:
            fields = _typed_fields(match.group('telemetry'))
            if 'frame' in fields:
                fields['frame_id'] = fields.pop('frame')
            frame_results.append(fields)
        match = DIAGNOSTICS.search(line)
        if match:
            fields = _typed_fields(match.group('telemetry'))
            if 'frame' in fields:
                fields['frame_id'] = fields.pop('frame')
            diagnostics.append(fields)
        match = DYNAMIC.search(line)
        if match:
            dynamic.append(_typed_fields(match.group('telemetry')))
        match = GPU_TIMING.search(line)
        if match:
            metadata = _typed_fields(match.group('timings'))
            timings = {
                key if key.endswith('_us') else f'{key}_us': value
                for key, value in metadata.items()
                if key not in ('time', 'frame', 'sdk_id', 'sdk_valid')
            }
            timings['time'] = metadata.get('time')
            timings['frame_id'] = metadata.get('frame')
            timings['sdk_id'] = metadata.get('sdk_id')
            timings['sdk_valid'] = metadata.get('sdk_valid')
            gpu_timings.append(timings)
    result = (frame_results[-1].get('result') if frame_results else
              presentations[-1].get('result') if presentations else None)
    results_by_frame = {entry.get('frame_id'): entry.get('result')
                        for entry in frame_results
                        if entry.get('frame_id') is not None}
    for entry in frames + frame_records:
        frame_id = entry.get('frame_id')
        if frame_id in results_by_frame:
            entry['result'] = results_by_frame[frame_id]
    for frame in frames:
        # A presentation banner is only a legacy, run-level result. Once
        # frame-specific result records exist, an unannotated frame must stay
        # unclassified instead of inheriting a later frame's result.
        if ('result' not in frame and result is not None and
                (not frame_results or frame.get('frame_id') is None)):
            frame['result'] = result
    return {
        'samples': samples,
        'frames': frames,
        'frame_records': frame_records,
        'presentations': presentations,
        'records': records,
        'frame_results': frame_results,
        'jitter': jitter_records,
        'diagnostics': diagnostics,
        'dynamic': dynamic,
        'gpu_timings': gpu_timings,
        'result': result,
    }


def summarize_gpu_timings(timings, frame_ids=None):
    """Summarize each existing VK FSR3 per-pass GPU timing independently."""
    if frame_ids:
        timings = [timing for timing in timings
                   if timing.get('frame_id') in frame_ids]
    keys = sorted({key for timing in timings for key in timing
                   if key.endswith('_us') and key != 'wall_usec'})
    return {key: summarize([timing[key] for timing in timings if key in timing])
            for key in keys}


def summarize(values):
    if not values:
        return {'count': 0}
    values = sorted(values)
    return {'count': len(values), 'mean': statistics.mean(values),
            'min': values[0], 'max': values[-1],
            'stdev': statistics.stdev(values) if len(values) > 1 else 0,
            'median': statistics.median(values),
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
    parser.add_argument('--frame-generation', choices=['0', '1'], default='0',
                        help='request FSR3 frame generation when supported')
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
    report = {
        'schema': 3,
        'demo_sha256': hashlib.sha256(args.demo.read_bytes()).hexdigest(),
        'settings': {key: str(value) for key, value in vars(args).items()},
        'tool': {'python': __import__('sys').version.split()[0]},
        'runs': [],
    }
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
                    r_fsr_sharpness='0',
                    r_fsr_frame_generation=(args.frame_generation if mode == 'fsr' else '0'),
                    r_fsr_dynamic='0',
                    vk_fsr_precision=args.precision, vk_fsr_subgroup=args.subgroup,
                    vk_fsr_profile='0', vk_fsr_debug='off', vk_fsr_benchmark='1',
                    vk_perf_stats='1',
                    con_notifytime='0', logfile='1', logfile_name='benchmark', logfile_flush='0')
                if gamelib is not None:
                    settings['sys_forcegamelib'] = str(gamelib.resolve())
                command = [str(args.binary.resolve())]
                for key, value in settings.items():
                    command += ['+set', key, value]
                command += ['+demo', 'benchmark', '+wait', str(args.frames + args.warmup + 300), '+quit']
                try:
                    completed = subprocess.run(command, capture_output=True,
                                               text=True, timeout=180)
                except subprocess.TimeoutExpired as exc:
                    (args.output / f'{repeat}-{mode}.log').write_text(
                        (exc.stdout or '') + (exc.stderr or ''))
                    raise RuntimeError(f'{mode} run timed out; see saved log') from exc
                log_path = Path(home) / 'baseq2/logs/benchmark.log'
                log = log_path.read_text(errors='replace') if log_path.exists() else completed.stdout + completed.stderr
                (args.output / f'{repeat}-{mode}.log').write_text(log)
                if completed.returncode or re.search(r'Vulkan error', log, re.IGNORECASE):
                    raise RuntimeError(f'{mode} run failed; see saved log')
                parsed = parse_benchmark_log(log)
                samples = parsed['samples']
                result = parsed['result']
                fallback_reasons = sorted(set(FALLBACK.findall(log)))
                captured[mode] = {
                    'samples': samples,
                    'result': result or ('native' if mode == 'native' else 'unknown'),
                    'frames': parsed['frames'],
                    'records': parsed['records'],
                    'frame_records': parsed['frame_records'],
                    'jitter': parsed['jitter'],
                    'diagnostics': parsed['diagnostics'],
                    'dynamic': parsed['dynamic'],
                    'gpu_timings': parsed['gpu_timings'],
                    'fallback': bool(fallback_reasons),
                    'fallback_reasons': fallback_reasons,
                    'fallback_reason': fallback_reasons[0] if fallback_reasons else None,
                }
                devices = re.findall(DEVICE, log)
                if devices:
                    report.setdefault('device', [])
                    report['device'] = sorted(set(report['device']) | set(devices))
                if not samples:
                    raise RuntimeError(f'{mode} run produced no FSR samples; see saved log')
        common = sorted(set(captured['native']['samples']) &
                        set(captured['fsr']['samples']), key=float)
        times = common[args.warmup:args.warmup + args.frames]
        if len(times) != args.frames:
            raise RuntimeError('not enough matching demo timestamps; use a longer demo or fewer frames')
        run = {
            'timestamps': times,
            'modes': {
                mode: {
                    'requested': mode,
                    'result': captured[mode]['result'],
                    'fallback': captured[mode]['fallback'],
                    'fallback_reasons': captured[mode]['fallback_reasons'],
                    'fallback_reason': captured[mode]['fallback_reason'],
                } for mode in captured
            },
        }
        run['frame_pacing'] = {
            mode: pacing_from_samples(captured[mode]['samples'], times)
            for mode in captured
        }
        run['renderer_completion_pacing'] = run['frame_pacing']
        run['simulation_time_pacing'] = pacing_from_timestamps(
            times, 'demo-simulation-time')
        # Keep the old key as a compatibility alias while making the two
        # pacing clocks explicit in the report.
        run['simulation_frame_pacing'] = run['simulation_time_pacing']
        for mode in captured:
            cpu, gpu, _wall, _frame = zip(*(captured[mode]['samples'][time]
                                            for time in times))
            gpu_samples = [value for value in gpu if value]
            run[mode] = {'cpu_us': summarize(cpu), 'gpu_us': summarize(gpu),
                         'frame_cost_us': summarize([max(c, g) for c, g in zip(cpu, gpu)]),
                         'timing_source': 'CPU/GPU' if all(gpu) else 'CPU-only',
                         'gpu_samples': len(gpu_samples),
                         'sample_count': len(times)}
            run[mode]['fps'] = fps_from_cost(run[mode]['frame_cost_us'])
            run[mode]['native_fps'] = run[mode]['fps'] if mode == 'native' else None
            run[mode]['upscaled_fps'] = (
                run[mode]['fps'] if mode == 'fsr' else None)
            run[mode]['generated_fps'] = (
                run[mode]['fps'] if captured[mode]['result'] == 'framegen' else None)
            run[mode]['frame_pacing'] = run['frame_pacing'][mode]
            run[mode]['renderer_completion_pacing'] = run['frame_pacing'][mode]
            run[mode]['simulation_time_pacing'] = run['simulation_time_pacing']
            run[mode]['fsr_frames'] = captured[mode]['frames']
            run[mode]['frame_telemetry'] = captured[mode]['records']
            run[mode]['fsr_frame_telemetry'] = captured[mode]['frame_records']
            run[mode]['jitter_telemetry'] = captured[mode]['jitter']
            run[mode]['diagnostics'] = captured[mode]['diagnostics']
            run[mode]['dynamic_telemetry'] = captured[mode]['dynamic']
            frame_ids = {captured[mode]['samples'][time][3]
                         for time in times
                         if captured[mode]['samples'][time][3] is not None}
            run[mode]['gpu_pass_timings'] = summarize_gpu_timings(
                captured[mode]['gpu_timings'], frame_ids)
        run['native_fps'] = run['native'].get('native_fps')
        run['upscaled_fps'] = run['fsr'].get('upscaled_fps')
        run['generated_fps'] = run['fsr'].get('generated_fps')
        run['fallback_reason'] = captured['fsr']['fallback_reason']
        report['runs'].append(run)
    (args.output / 'report.json').write_text(json.dumps(report, indent=2))
    print(args.output / 'report.json')


if __name__ == '__main__':
    main()
