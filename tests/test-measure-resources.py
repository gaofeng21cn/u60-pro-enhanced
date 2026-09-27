#!/usr/bin/env python3
"""The renderer and its same-named watcher must both be counted."""
import importlib.util
import pathlib

path = pathlib.Path(__file__).resolve().parents[1] / 'scripts/measure-resources.py'
spec = importlib.util.spec_from_file_location('resources', path)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
before = m.parse('''member u60-panel 20 10
proc u60-panel rss_kb 20000
proc u60-panel ticks 500
member u60-panel 21 11
proc u60-panel rss_kb 600
proc u60-panel ticks 20
''')['processes']['u60-panel']
assert before['rss_kb'] == 20600
assert before['ticks'] == 520
after = dict(before, ticks=540)
assert m.tick_delta(before, after) == 20
assert m.tick_delta(before, dict(after, members=['20:12', '21:11'])) is None
assert m.tick_delta(before, dict(after, members=['21:11'])) is None
assert m.tick_delta(before, dict(after, ticks=5)) is None
print('PASS: aggregate renderer/watcher memory and reject CPU deltas across restarts')
