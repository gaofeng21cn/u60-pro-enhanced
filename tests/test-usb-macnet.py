#!/usr/bin/env python3
"""USB diagnostics must remain read-only, including rejected old entry points."""
import json
import pathlib
import subprocess
import tempfile
import unittest

SOURCE = pathlib.Path(__file__).resolve().parents[1] / 'panel/enable-usb-macnet.sh'

class UsbDiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.gadget = self.root / 'gadget'
        self.net = self.root / 'net'
        self.udcs = self.root / 'udcs'
        (self.gadget / 'configs/c.1').mkdir(parents=True)
        (self.udcs / 'controller').mkdir(parents=True)
        self.net.mkdir()
        self.script = self.root / 'script'
        self.script.write_text(SOURCE.read_text().replace(
            'GADGET=/sys/kernel/config/usb_gadget/g1', f'GADGET={self.gadget}'
        ).replace('NET=/sys/class/net', f'NET={self.net}').replace(
            'UDCS=/sys/class/udc', f'UDCS={self.udcs}'))

    def call(self, action):
        result = subprocess.run(['sh', str(self.script), action],
                                text=True, capture_output=True, timeout=2)
        self.assertEqual(result.stderr, '')
        return result.returncode, json.loads(result.stdout)

    def configure(self, mode='rndis', carrier='1', bridge=True):
        target = 'gsi.rndis' if mode == 'rndis' else 'ncm.0'
        (self.gadget / 'configs/c.1/f3').symlink_to(f'../../functions/{target}')
        (self.gadget / 'configs/c.1/f6').symlink_to('../../functions/ffs.adb')
        (self.gadget / 'UDC').write_text('controller\n')
        (self.udcs / 'controller/state').write_text('configured\n')
        interface = self.net / ('rndis0' if mode == 'rndis' else 'usb0')
        interface.mkdir()
        (interface / 'carrier').write_text(carrier)
        if bridge:
            (interface / 'master').symlink_to('../br-lan')

    def snapshot(self):
        return {str(p.relative_to(self.root)): ('link', str(p.readlink())) if p.is_symlink()
                else ('file', p.read_bytes()) if p.is_file() else ('dir', '')
                for p in self.root.rglob('*')}

    def test_status_never_changes_files_or_bridge(self):
        self.configure()
        before = self.snapshot()
        rc, status = self.call('status')
        self.assertEqual(rc, 0)
        self.assertTrue(all(status[k] for k in ('ok','bound','configured','carrier','bridged','adb_function')))
        self.assertFalse(status['switch_available'])
        self.assertFalse(status['ncm_present'])
        self.assertEqual(before, self.snapshot())

    def test_present_ncm_does_not_enable_switching_or_change_active_mode(self):
        self.configure('rndis', '0')
        (self.gadget / 'functions/ncm.0').mkdir(parents=True)
        before = self.snapshot()
        _, status = self.call('status')
        self.assertTrue(status['ncm_present'])
        self.assertFalse(status['ncm_composition'])
        self.assertFalse(status['switch_available'])
        self.assertEqual(status['mode'], 'rndis')
        self.assertEqual(before, self.snapshot())

    def test_missing_host_is_not_working_lan(self):
        self.configure('rndis', '0')
        (self.udcs / 'controller/state').write_text('not attached\n')
        _, status = self.call('status')
        self.assertTrue(status['bound'])
        self.assertFalse(status['configured'])
        self.assertFalse(status['carrier'])
        self.assertFalse(status['bridged'])

    def test_unknown_and_unbound_are_distinct(self):
        _, status = self.call('status')
        self.assertFalse(status['ok'])
        self.configure()
        (self.gadget / 'UDC').write_text('')
        _, status = self.call('status')
        self.assertTrue(status['ok'])
        self.assertFalse(status['bound'])
        self.assertFalse(status['configured'])

    def test_old_mutation_entry_points_cannot_reconfigure_usb(self):
        self.configure()
        before = self.snapshot()
        for action in ('enable', 'start', 'confirm', 'restore', 'rndis', 'invalid'):
            rc, result = self.call(action)
            self.assertNotEqual(rc, 0)
            self.assertFalse(result['ok'])
            self.assertEqual(before, self.snapshot())

if __name__ == '__main__':
    unittest.main()
