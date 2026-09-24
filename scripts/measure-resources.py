#!/usr/bin/env python3
"""Sample U60 Pro resource use over ADB and write a local report.

Read-only: the script only reads /proc, /sys and the core's local API. It never
changes device state, so it is safe to run while the device is in use.

Run one scenario at a time and label it, then compare the reports. The plan
calls for four: screen on, screen off, background with no traffic, and an active
proxy load. Memory in particular only grows under real traffic, so a single idle
reading is not a resource verdict.
"""
import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys
import time


def adb_run(adb, serial, script, timeout=60):
    cmd = [adb] + (['-s', serial] if serial else []) + ['shell', script]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if p.returncode != 0:
        raise SystemExit('Device read failed: ' + (p.stderr or p.stdout)[:200])
    return p.stdout


SAMPLE = r'''
awk '/^cpu / {print "cpu_total", $2+$3+$4+$5+$6+$7+$8+$9}' /proc/stat
awk '/^MemTotal:/ {print "mem_total", $2}' /proc/meminfo
awk '/^MemAvailable:/ {print "mem_available", $2}' /proc/meminfo
awk '{print "uptime", $1}' /proc/uptime
for n in mihomo tailscaled u60-panel panel-web; do
 for p in $(pidof "$n" 2>/dev/null); do
  awk -v who="$n" '/^VmRSS:/ {print "proc", who, "rss_kb", $2}' /proc/$p/status 2>/dev/null
  awk -v who="$n" '/^Threads:/ {print "proc", who, "threads", $2}' /proc/$p/status 2>/dev/null
  awk -v who="$n" '{print "proc", who, "ticks", $14+$15}' /proc/$p/stat 2>/dev/null
 done
done
'''

# The screen control program reports the temperature and load the UI itself
# shows, which is the reading the user can compare against.
PANEL_STATE = ("printf '%s\\n' '{\"action\":\"state\",\"args\":{}}' "
               "| /data/u60-panel/panel-control")


def parse(out):
    values = {}
    names = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[0] == 'proc':
            _, who, metric, raw = parts
            try:
                names.setdefault(who, {})[metric] = float(raw)
            except ValueError:
                pass
            continue
        if len(parts) != 2:
            continue
        key, raw = parts
        try:
            value = float(raw)
        except ValueError:
            continue
        values[key] = value
    values['processes'] = names
    return values


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--adb', default=shutil.which('adb'))
    ap.add_argument('--serial')
    ap.add_argument('--scenario', required=True,
                    help='label for this run, e.g. screen-on-idle, proxy-load')
    ap.add_argument('--seconds', type=int, default=60)
    ap.add_argument('--interval', type=float, default=3.0)
    ap.add_argument('--output', type=pathlib.Path)
    a = ap.parse_args()
    if not a.adb:
        ap.error('Provide --adb /path/to/adb')
    if a.seconds < 5 or a.interval < 1:
        ap.error('Use at least 5 seconds and a 1 second interval')

    first = parse(adb_run(a.adb, a.serial, SAMPLE))
    started = time.time()
    samples = []
    while time.time() - started < a.seconds:
        time.sleep(a.interval)
        try:
            samples.append(parse(adb_run(a.adb, a.serial, SAMPLE)))
        except SystemExit as error:
            print('Sampling stopped: ' + str(error), file=sys.stderr)
            break
    last = samples[-1] if samples else first
    elapsed = max(time.time() - started, 0.001)

    # CPU per process, normalised so 100 means one busy core. The window average
    # hides a short burst, so the per-interval peak is reported as well.
    cores = int(adb_run(a.adb, a.serial, 'grep -c ^processor /proc/cpuinfo').strip() or 1)
    cpu = {}
    peak_cpu = {}
    total_delta = last.get('cpu_total', 0) - first.get('cpu_total', 0)
    for name, metrics in last.get('processes', {}).items():
        before = first.get('processes', {}).get(name, {})
        if 'ticks' not in metrics or 'ticks' not in before or total_delta <= 0:
            continue
        share = (metrics['ticks'] - before['ticks']) / total_delta
        cpu[name] = round(share * cores * 100, 2)
    timeline = [first] + samples
    for previous, current in zip(timeline, timeline[1:]):
        step = current.get('cpu_total', 0) - previous.get('cpu_total', 0)
        if step <= 0:
            continue
        for name, metrics in current.get('processes', {}).items():
            was = previous.get('processes', {}).get(name, {}).get('ticks')
            if was is None or 'ticks' not in metrics:
                continue
            value = (metrics['ticks'] - was) / step * cores * 100
            peak_cpu[name] = max(peak_cpu.get(name, 0), value)
    peak_cpu = {k: round(v, 2) for k, v in peak_cpu.items()}

    peak_rss = {}
    for sample in samples or [first]:
        for name, metrics in sample.get('processes', {}).items():
            peak_rss[name] = max(peak_rss.get(name, 0), metrics.get('rss_kb', 0))

    total = last.get('mem_total', 0)
    available = last.get('mem_available', 0)
    thermal = []
    raw_thermal = adb_run(a.adb, a.serial,
                          "cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | head -4")
    for token in raw_thermal.split():
        try:
            value = int(token)
        except ValueError:
            continue
        celsius = value / 1000 if abs(value) > 1000 else float(value)
        # Some zones report a placeholder; keep only plausible readings.
        if -30 <= celsius <= 150:
            thermal.append(round(celsius, 1))

    panel_view = {}
    try:
        raw_state = adb_run(a.adb, a.serial, PANEL_STATE, timeout=30)
        state = json.loads(raw_state.strip().splitlines()[-1])
        data = state.get('data', {})
        for key in ['temperature', 'cpu_percent', 'memory_percent', 'battery', 'network_profile']:
            if key in data:
                panel_view[key] = data[key]
    except Exception as error:  # the panel may be released; that is not a failure
        panel_view['unavailable'] = str(error)[:80]

    report = {
        'scenario': a.scenario,
        'sampled_seconds': round(elapsed),
        'samples': len(samples),
        'cpu_percent_one_core_100': cpu,
        'peak_cpu_percent_one_core_100': peak_cpu,
        'peak_rss_mib': {k: round(v / 1024, 1) for k, v in peak_rss.items()},
        'memory': {
            'total_mib': round(total / 1024, 1),
            'available_mib': round(available / 1024, 1),
            'available_percent': round(available / total * 100, 1) if total else None,
        },
        'thermal_c': thermal,
        'panel_reported': panel_view,
        'uptime_seconds': last.get('uptime'),
        'note': 'Read-only sample. A single idle reading does not prove long-run stability.',
    }
    text = json.dumps(report, ensure_ascii=False, indent=2)
    print(text)
    if a.output:
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(text + '\n')
        print('Wrote ' + str(a.output), file=sys.stderr)


if __name__ == '__main__':
    main()
