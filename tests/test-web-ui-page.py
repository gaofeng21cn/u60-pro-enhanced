#!/usr/bin/env python3
"""Behavior of the standalone Mihomo console page without a browser.

The page is deployed as Mihomo's external-ui dashboard, so the host cannot
exercise it end to end. This runs its real script inside a tiny stub DOM and
checks the pure helpers plus the leaf-selector walk.
"""
import json
import pathlib
import re
import subprocess
import tempfile
import textwrap
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PAGE = ROOT / "ui" / "index.html"

DRIVER = textwrap.dedent(
    """
    const fs = require('fs'), vm = require('vm');
    const html = fs.readFileSync(process.argv[2], 'utf8');
    const source = /<script>([\\s\\S]*?)<\\/script>/.exec(html)[1];
    function element() {
      return {
        value: '', textContent: '', innerHTML: '', className: '', dataset: {}, style: {},
        classList: { add() {}, remove() {}, toggle() {} },
        addEventListener() {}, click() {},
      };
    }
    const nodes = {};
    const store = { getItem: () => null, setItem() {} };
    const context = {
      document: { getElementById: (id) => nodes[id] || (nodes[id] = element()), querySelectorAll: () => [] },
      localStorage: store, sessionStorage: store,
      location: { protocol: 'http:', hostname: 'localhost', port: '', origin: 'http://localhost' },
      fetch: () => Promise.reject(new Error('offline')), setTimeout, clearTimeout, console,
    };
    vm.createContext(context);
    vm.runInContext(source, context, { filename: 'ui/index.html' });
    const groups = {
      GLOBAL: { type: 'Selector', now: 'Main', all: ['Main'] },
      Main: { type: 'Selector', now: 'Leaf 01', all: ['Leaf 01'] },
      'Leaf 01': { type: 'Vless' },
    };
    const cycle = {
      Main: { type: 'Selector', now: 'Mojie', all: ['Mojie'] },
      Mojie: { type: 'Selector', now: 'Main', all: ['Main'] },
    };
    Promise.all([
      context.activeLeafPath(groups, 'global'),
      context.activeLeafPath(cycle, 'global'),
      context.activeLeafPath(groups, 'direct'),
    ]).then(([path, looped, direct]) => {
      console.log(JSON.stringify({
        path, looped, direct,
        escaped: context.esc('<b>&"'),
        bytes: context.fmtBytes(1536),
        mode: context.modeLabel('rule'),
      }));
    });
    """
)


class UiPageTests(unittest.TestCase):
    def test_script_parses(self):
        script = re.search(r"<script>(.*?)</script>", PAGE.read_text(encoding="utf-8"), re.S)
        self.assertIsNotNone(script)
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "page.js"
            path.write_text(script.group(1), encoding="utf-8")
            result = subprocess.run(["node", "--check", str(path)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_helpers_and_active_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            driver = pathlib.Path(tmp) / "driver.js"
            driver.write_text(DRIVER, encoding="utf-8")
            result = subprocess.run(["node", str(driver), str(PAGE)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        got = json.loads(result.stdout.strip().splitlines()[-1])
        self.assertEqual(got["path"], ["GLOBAL", "Main"])
        self.assertEqual(got["looped"], [])
        self.assertEqual(got["direct"], [])
        self.assertEqual(got["escaped"], "&lt;b&gt;&amp;&quot;")
        self.assertEqual(got["bytes"], "1.5 KB")
        self.assertEqual(got["mode"], "规则分流")


if __name__ == "__main__":
    unittest.main()
