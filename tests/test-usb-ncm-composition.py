#!/usr/bin/env python3
"""The unqualified composition must refuse every direct invocation."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'panel/usb-ncm-composition.sh'


class NcmCompositionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.g = self.root / 'sys/kernel/config/usb_gadget/g1'
        self.u = self.root / 'sys/class/udc/controller'
        for p in ['configs/c.1/strings/0x409', 'functions/ncm.0',
                  'functions/ffs.diag', 'functions/cser.nmea.1',
                  'functions/cser.dun.0', 'functions/mass_storage.0',
                  'functions/ffs.adb', 'functions/gsi.dpl',
                  'functions/qdss.qdss_mdm', 'strings/0x409']:
            (self.g / p).mkdir(parents=True)
        self.u.mkdir(parents=True)
        (self.root / 'tmp').mkdir()
        (self.u / 'state').write_text('configured\n')
        (self.g / 'UDC').write_text('controller\n')
        (self.g / 'strings/0x409/serialnumber').write_text('0123456789ABCDEF\n')
        (self.g / 'idVendor').write_text('0x19d2\n')
        (self.g / 'idProduct').write_text('0x1404\n')
        (self.g / 'bDeviceClass').write_text('0x00\n')
        (self.g / 'configs/c.1/strings/0x409/configuration').write_text('\n')
        for index, target in enumerate(
                ['gsi.rndis', 'ffs.diag', 'cser.nmea.1', 'cser.dun.0',
                 'mass_storage.0', 'ffs.adb', 'gsi.dpl', 'qdss.qdss_mdm'], 1):
            (self.g / 'functions' / target).mkdir(exist_ok=True)
            (self.g / 'configs/c.1' / f'f{index}').symlink_to(
                '../../../../usb_gadget/g1/functions/' + target)
        self.udcs = self.root / 'sys/class/udc'
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.compositions = self.root / 'sbin/usb/compositions'
        self.compositions.mkdir(parents=True)
        self.script = self.root / '908C'
        source = SOURCE.read_text().replace(
            'G=${USB_NCM_GADGET:-/sys/kernel/config/usb_gadget/g1}',
            f'G=${{USB_NCM_GADGET:-{self.g}}}').replace(
            'UDCS=${USB_NCM_UDCS:-/sys/class/udc}',
            f'UDCS=${{USB_NCM_UDCS:-{self.udcs}}}').replace(
            'MARKER=${USB_NCM_BIND_MARKER:-/tmp/usb_bind_in_progress}',
            f'MARKER=${{USB_NCM_BIND_MARKER:-{self.root}/tmp/bind}}')
        self.script.write_text(source)
        self.script.chmod(0o700)

    def call(self, *args):
        env = {'PATH': '/usr/bin:/bin', 'USB_NCM_GADGET': str(self.g),
               'USB_NCM_UDCS': str(self.udcs), 'USB_NCM_BIND_MARKER': str(self.root / 'tmp/bind')}
        return subprocess.run(['sh', str(self.script), *args], env=env,
                              text=True, capture_output=True, timeout=3)

    def test_direct_composition_cannot_bypass_quarantine(self):
        def snapshot():
            return {str(p.relative_to(self.root)):
                    str(p.readlink()) if p.is_symlink() else
                    p.read_bytes() if p.is_file() else None
                    for p in self.root.rglob('*')}
        before = snapshot()
        for args in ((), ('n', '0.2', 'n'), ('n', '0.2', 'y')):
            with self.subTest(args=args):
                result = self.call(*args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('disabled', result.stderr)
                self.assertEqual(before, snapshot())


if __name__ == '__main__':
    unittest.main()
