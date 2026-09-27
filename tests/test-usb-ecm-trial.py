#!/usr/bin/env python3
"""Regression: stale transactions and former overrides cannot invoke USB writers."""
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class EcmQuarantineTests(unittest.TestCase):
    def test_stale_transactions_and_overrides_do_not_execute_writers(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            runtime = root / 'tmp/u60-ecm-trial'
            (runtime / 'snapshot').mkdir(parents=True)
            (runtime / 'snapshot/udc').write_text('controller\n')
            marker = root / 'writer-called'
            owner = root / 'owner'
            owner.write_text('#!/bin/sh\ntouch "'+str(marker)+'"\n')
            owner.chmod(0o700)
            script = ROOT / 'panel/usb-ecm-trial.sh'
            for state in ('pending', 'active', 'restored', 'failed'):
                (runtime / 'state').write_text(state)
                before = {str(p): p.read_bytes() for p in root.rglob('*') if p.is_file()}
                for test_root in (str(root), '/'):
                    env = dict(os.environ, U60_ECM_TEST_ROOT=test_root,
                               USB_COMPOSITION=str(owner), USB_ECM_INIT=str(owner),
                               USB_IP=str(owner), ECM_RELEASE_ENABLED='true')
                    for action in ('start', 'supervise', 'confirm', 'restore', 'restore-trial', 'invalid'):
                        with self.subTest(state=state, action=action, test_root=test_root):
                            result = subprocess.run(['sh', str(script), action], env=env,
                                                    text=True, capture_output=True, timeout=2)
                            self.assertNotEqual(result.returncode, 0)
                            self.assertFalse(json.loads(result.stdout)['ok'])
                            self.assertEqual(result.stderr, '')
                            self.assertFalse(marker.exists())
                self.assertEqual(before, {str(p): p.read_bytes() for p in root.rglob('*') if p.is_file()})

if __name__ == '__main__':
    unittest.main()
