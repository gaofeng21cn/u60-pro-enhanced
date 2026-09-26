#!/usr/bin/env python3
"""NCM quarantine tests; these do not qualify USB switching or rollback."""
import json, os, pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class NcmTrialTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.g = self.root / 'sys/kernel/config/usb_gadget/g1'
        self.n = self.root / 'sys/class/net'
        self.u = self.root / 'sys/class/udc/controller'
        for p in ['configs/c.1', 'functions/ncm.0', 'functions/gsi.rndis',
                  'functions/ffs.adb', 'functions/ffs.diag', 'functions/cser.nmea.1',
                  'functions/cser.dun.0', 'functions/mass_storage.0',
                  'functions/gsi.dpl', 'functions/qdss.qdss_mdm']:
            (self.g / p).mkdir(parents=True)
        self.u.mkdir(parents=True)
        (self.root / 'dev/usb-ffs/adb').mkdir(parents=True)
        (self.root / 'dev/usb-ffs/adb/ep0').touch()
        for name, target in [('f1','gsi.rndis'),('f2','ffs.diag'),('f3','cser.nmea.1'),
                             ('f4','cser.dun.0'),('f5','mass_storage.0'),
                             ('f6','ffs.adb'),('f7','gsi.dpl'),('f8','qdss.qdss_mdm')]:
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
        (self.bin / 'start-stop-daemon').write_text('#!/bin/sh\nwhile [ "$1" != "--" ]; do shift; done; shift; "$@" &\n'); (self.bin / 'start-stop-daemon').chmod(0o700)
        (self.bin / 'ip').write_text('#!/bin/sh\nln -sf ../br-lan "$U60_NCM_TEST_ROOT/sys/class/net/$4/master"\n'); (self.bin / 'ip').chmod(0o700)
        self.candidate = self.root / '908C'; self.candidate.write_text('#!/bin/sh\nexit 0\n'); self.candidate.chmod(0o700)
        self.owner = self.bin / 'usb_composition'
        self.owner.write_text('#!/bin/sh\n[ "$1" = 908C ] || exit 1\nG="$U60_NCM_TEST_ROOT/sys/kernel/config/usb_gadget/g1"\nprintf "\\n" > "$G/UDC"\nfor link in "$G"/configs/c.1/f*; do [ -L "$link" ] && rm "$link"; done\nln -s ../../../../usb_gadget/g1/functions/ncm.0 "$G/configs/c.1/f1"\nprintf "%s\\n" controller > "$G/UDC"\n')
        self.owner.chmod(0o700)
        source = (ROOT / 'panel/usb-ncm-trial.sh').read_text()
        source = source.replace('G=/sys/kernel/config/usb_gadget/g1', 'G=' + str(self.g))
        source = source.replace('N=/sys/class/net', 'N=' + str(self.n))
        source = source.replace('U=/sys/class/udc', 'U=' + str(self.root / 'sys/class/udc'))
        source = source.replace('R=/tmp/u60-ncm-trial', 'R=' + str(self.root / 'tmp/u60-ncm-trial'))
        source = source.replace('ADB_FFS=/dev/usb-ffs/adb', 'ADB_FFS=' + str(self.root / 'dev/usb-ffs/adb'))
        source = source.replace('ubus -t 5 call zwrt_zte_mdm.api get_zwrt_common_info', 'echo BD_CNMU5250V1.0.0B31')
        self.script = self.root / 'ncm.sh'; self.script.write_text(source)
        self.env = dict(os.environ, PATH=str(self.bin) + ':' + os.environ['PATH'],
                        U60_NCM_TEST_ROOT=str(self.root), USB_COMPOSITION=str(self.owner),
                        USB_NCM_COMPOSITION=str(self.candidate))

    def call(self, action):
        return subprocess.run(['sh', str(self.script), action], env=self.env, text=True,
                              capture_output=True, timeout=4)

    def test_status_is_read_only(self):
        before = {str(p.relative_to(self.root)): p.read_bytes() if p.is_file() else str(p.readlink()) if p.is_symlink() else '' for p in self.root.rglob('*')}
        r = self.call('status'); self.assertEqual(r.returncode, 0)
        self.assertTrue(json.loads(r.stdout)['ncm_present'])
        after = {str(p.relative_to(self.root)): p.read_bytes() if p.is_file() else str(p.readlink()) if p.is_symlink() else '' for p in self.root.rglob('*')}
        self.assertEqual(before, after)

    def test_all_mutating_entries_are_quarantined(self):
        # Even a stale pending transaction cannot re-enter the unsafe writer.
        runtime = self.root / 'tmp/u60-ncm-trial'
        runtime.mkdir(parents=True)
        (runtime / 'state').write_text('pending')
        def snapshot():
            return {str(p.relative_to(self.root)):
                    str(p.readlink()) if p.is_symlink() else
                    p.read_bytes() if p.is_file() else None
                    for p in self.root.rglob('*')}
        before = snapshot()
        for action in ('start', 'supervise', 'confirm', 'restore', 'invalid'):
            with self.subTest(action=action):
                result = self.call(action)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(json.loads(result.stdout)['ok'])
                self.assertIn('ADB', json.loads(result.stdout)['message'])
                self.assertEqual(before, snapshot())

if __name__ == '__main__': unittest.main()
