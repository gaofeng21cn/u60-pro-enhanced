#!/usr/bin/env python3
"""The ECM maintenance transaction must restore through the factory owner."""
import json, os, pathlib, subprocess, tempfile, time, unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class EcmTrialTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.g = self.root / 'sys/kernel/config/usb_gadget/g1'
        self.n = self.root / 'sys/class/net'
        self.u = self.root / 'sys/class/udc/controller'
        for p in ['configs/c.1', 'functions/gsi.rndis', 'functions/gsi.ecm', 'functions/ffs.adb']:
            (self.g / p).mkdir(parents=True)
        self.u.mkdir(parents=True); (self.u / 'state').write_text('configured\n')
        (self.g / 'UDC').write_text('controller\n')
        (self.g / 'functions/gsi.ecm/ifname').write_text('ecm0\n')
        (self.root / 'dev/usb-ffs/adb').mkdir(parents=True)
        (self.root / 'dev/usb-ffs/adb/ep0').touch()
        (self.g / 'configs/c.1/f1').symlink_to('../../../../usb_gadget/g1/functions/gsi.rndis')
        (self.g / 'configs/c.1/f6').symlink_to('../../../../usb_gadget/g1/functions/ffs.adb')
        for name in ['ecm0', 'br-lan']:
            (self.n / name).mkdir(parents=True)
        (self.n / 'ecm0/carrier').write_text('1\n')
        self.bin = self.root / 'bin'; self.bin.mkdir()
        (self.bin / 'flock').write_text('#!/bin/sh\nexit 0\n'); (self.bin / 'flock').chmod(0o700)
        (self.bin / 'pidof').write_text('#!/bin/sh\n[ "$1" = adbd ]\n'); (self.bin / 'pidof').chmod(0o700)
        (self.bin / 'ip').write_text(
            '#!/bin/sh\nprintf "%s\\n" "$*" >> "$U60_ECM_TEST_ROOT/ip.log"\n'
            'if [ "$1" = link ] && [ "$2" = set ] && [ "$3" = dev ] && [ "$5" = master ]; then '
            'ln -sf ../$6 "$U60_ECM_TEST_ROOT/sys/class/net/$4/master"; fi\n'
        ); (self.bin / 'ip').chmod(0o700)
        self.owner = self.bin / 'usb_composition'
        self.owner.write_text(
            '#!/bin/sh\nG="$U60_ECM_TEST_ROOT/sys/kernel/config/usb_gadget/g1"\n'
            'old=$(readlink "$G/configs/c.1/f1" 2>/dev/null || true)\n'
            'for x in "$G"/configs/c.1/f*; do [ -L "$x" ] && rm "$x"; done\n'
            'if [ "$1" = 9059 ] && [ "$old" != "../../../../usb_gadget/g1/functions/gsi.ecm" ]; then '
            'ln -s ../../../../usb_gadget/g1/functions/gsi.ecm "$G/configs/c.1/f1"; '
            'else ln -s ../../../../usb_gadget/g1/functions/gsi.rndis "$G/configs/c.1/f1"; '
            'ln -s ../../../../usb_gadget/g1/functions/ffs.adb "$G/configs/c.1/f6"; fi\n'
            'printf \'controller\\n\' > "$G/UDC"\n'
        ); self.owner.chmod(0o700)
        source = (ROOT / 'panel/usb-ecm-trial.sh').read_text()
        source = source.replace('ECM_RELEASE_ENABLED=false', 'ECM_RELEASE_ENABLED=true')
        self.script = self.root / 'ecm.sh'; self.script.write_text(source); self.script.chmod(0o700)
        self.env = dict(os.environ, PATH=str(self.bin) + ':' + os.environ['PATH'],
                        U60_ECM_TEST_ROOT=str(self.root), USB_COMPOSITION=str(self.owner),
                        USB_IP=str(self.bin / 'ip'))

    def call(self, action, timeout=20):
        return subprocess.run(['sh', str(self.script), action], env=self.env,
                              text=True, capture_output=True, timeout=timeout)

    def test_trial_switch_confirm_and_owner_restore(self):
        started = self.call('start')
        self.assertEqual(started.returncode, 0, started.stderr)
        self.assertTrue(json.loads(started.stdout)['ok'])
        for _ in range(100):
            if ((self.g / 'configs/c.1/f1').is_symlink() and
                    (self.g / 'configs/c.1/f1').readlink().name == 'gsi.ecm' and
                    (self.n / 'ecm0/master').is_symlink()):
                break
            time.sleep(.1)
        self.assertEqual((self.g / 'configs/c.1/f1').readlink().name, 'gsi.ecm')
        confirmed = self.call('confirm')
        self.assertEqual(confirmed.returncode, 0, confirmed.stderr + confirmed.stdout)
        restored = self.call('restore')
        self.assertEqual(restored.returncode, 0, restored.stderr + restored.stdout)
        self.assertEqual((self.g / 'configs/c.1/f1').readlink().name, 'gsi.rndis')
        self.assertEqual((self.g / 'configs/c.1/f6').readlink().name, 'ffs.adb')
        self.assertEqual((self.root / 'tmp/u60-ecm-trial/state').read_text(), 'restored')

if __name__ == '__main__':
    unittest.main()
