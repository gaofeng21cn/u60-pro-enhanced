#!/usr/bin/env python3
"""NCM transaction tests; no device access or USB writes."""
import json, os, pathlib, subprocess, tempfile, time, unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class NcmTrialTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.g = self.root / 'sys/kernel/config/usb_gadget/g1'
        self.n = self.root / 'sys/class/net'
        self.u = self.root / 'sys/class/udc/controller'
        for p in ['configs/c.1', 'functions/ncm.0', 'functions/gsi.rndis',
                  'functions/ffs.adb', 'functions/ffs.diag', 'functions/mass_storage.0']:
            (self.g / p).mkdir(parents=True)
        self.u.mkdir(parents=True)
        (self.root / 'dev/usb-ffs/adb').mkdir(parents=True)
        (self.root / 'dev/usb-ffs/adb/ep0').touch()
        for name, target in [('f1','gsi.rndis'),('f2','ffs.diag'),('f3','mass_storage.0'),('f4','ffs.adb')]:
            (self.g / 'configs/c.1' / name).symlink_to('../../../../usb_gadget/g1/functions/' + target)
        (self.g / 'UDC').write_text('controller\n')
        (self.u / 'state').write_text('configured\n')
        (self.g / 'functions/ncm.0/ifname').write_text('usb0\n')
        for name in ['usb0', 'br-lan']:
            (self.n / name).mkdir(parents=True)
        (self.n / 'usb0/carrier').write_text('1\n')
        self.bin = self.root / 'bin'; self.bin.mkdir()
        (self.bin / 'flock').write_text('#!/bin/sh\nexit 0\n'); (self.bin / 'flock').chmod(0o700)
        (self.bin / 'pidof').write_text('#!/bin/sh\n[ "$1" = adbd ]\n'); (self.bin / 'pidof').chmod(0o700)
        (self.bin / 'uname').write_text('#!/bin/sh\necho 5.15.194-perf\n'); (self.bin / 'uname').chmod(0o700)
        (self.bin / 'ubus').write_text('#!/bin/sh\necho \'{"wa_inner_version":"BD_CNMU5250V1.0.0B31"}\'\n'); (self.bin / 'ubus').chmod(0o700)
        (self.bin / 'jsonfilter').write_text('#!/bin/sh\necho BD_CNMU5250V1.0.0B31\n'); (self.bin / 'jsonfilter').chmod(0o700)
        (self.bin / 'sleep').write_text('#!/bin/sh\nexit 0\n'); (self.bin / 'sleep').chmod(0o700)
        (self.bin / 'ip').write_text('#!/bin/sh\nln -sf ../br-lan "$U60_NCM_TEST_ROOT/sys/class/net/$4/master"\n'); (self.bin / 'ip').chmod(0o700)
        source = (ROOT / 'panel/usb-ncm-trial.sh').read_text()
        source = source.replace('G=/sys/kernel/config/usb_gadget/g1', 'G=' + str(self.g))
        source = source.replace('N=/sys/class/net', 'N=' + str(self.n))
        source = source.replace('U=/sys/class/udc', 'U=' + str(self.root / 'sys/class/udc'))
        source = source.replace('R=/tmp/u60-ncm-trial', 'R=' + str(self.root / 'tmp/u60-ncm-trial'))
        source = source.replace('ADB_FFS=/dev/usb-ffs/adb', 'ADB_FFS=' + str(self.root / 'dev/usb-ffs/adb'))
        source = source.replace('ubus -t 5 call zwrt_zte_mdm.api get_zwrt_common_info', 'echo BD_CNMU5250V1.0.0B31')
        self.script = self.root / 'ncm.sh'; self.script.write_text(source)
        self.env = dict(os.environ, PATH=str(self.bin) + ':' + os.environ['PATH'], U60_NCM_TEST_ROOT=str(self.root))

    def call(self, action):
        return subprocess.run(['sh', str(self.script), action], env=self.env, text=True,
                              capture_output=True, timeout=4)

    def test_status_is_read_only(self):
        before = {str(p.relative_to(self.root)): p.read_bytes() if p.is_file() else str(p.readlink()) if p.is_symlink() else '' for p in self.root.rglob('*')}
        r = self.call('status'); self.assertEqual(r.returncode, 0)
        self.assertTrue(json.loads(r.stdout)['ncm_present'])
        after = {str(p.relative_to(self.root)): p.read_bytes() if p.is_file() else str(p.readlink()) if p.is_symlink() else '' for p in self.root.rglob('*')}
        self.assertEqual(before, after)

    def test_start_rejects_non_rndis_composition(self):
        (self.g / 'configs/c.1/f1').unlink(); (self.g / 'configs/c.1/f1').symlink_to('../../../../usb_gadget/g1/functions/ffs.adb')
        r = self.call('start'); self.assertNotEqual(r.returncode, 0)
        self.assertIn('RNDIS', r.stdout)

    def test_restore_rebuilds_every_saved_function(self):
        r = self.call('start'); self.assertEqual(r.returncode, 0, r.stdout)
        # The supervisor is intentionally exercised directly in the fake root.
        self.assertTrue((self.root / 'tmp/u60-ncm-trial/snapshot/links').exists())
        (self.g / 'configs/c.1/f2').unlink()
        r = self.call('restore'); self.assertEqual(r.returncode, 0, r.stdout)
        links = {p.name: str(p.readlink()) for p in (self.g / 'configs/c.1').iterdir() if p.is_symlink()}
        self.assertEqual(set(links), {'f1','f2','f3','f4'})
        self.assertTrue(links['f4'].endswith('ffs.adb'))

    def test_supervisor_timeout_restores_the_full_composition(self):
        r = self.call('start'); self.assertEqual(r.returncode, 0, r.stdout)
        state = self.root / 'tmp/u60-ncm-trial/state'
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and state.read_text() not in ('restored', 'failed'):
            time.sleep(0.01)
        self.assertEqual(state.read_text(), 'restored')
        links = {p.name: str(p.readlink()) for p in (self.g / 'configs/c.1').iterdir() if p.is_symlink()}
        self.assertEqual(set(links), {'f1','f2','f3','f4'})
        self.assertEqual((self.g / 'UDC').read_text().strip(), 'controller')

if __name__ == '__main__': unittest.main()
