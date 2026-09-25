#!/usr/bin/env python3
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).parents[1] / 'panel/usb-role.sh'


class RoleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.panel = self.root / 'data/u60-panel'
        self.panel.mkdir(parents=True)
        (self.root / 'bin').mkdir()
        (self.root / 'tmp').mkdir()
        (self.root / 'proc').mkdir()
        (self.root / 'proc/uptime').write_text('200.00 0')
        self.env = dict(os.environ, U60_USB_TEST_ROOT=str(self.root), TEST_ROOT=str(self.root))
        self.env['PATH'] = str(self.root / 'bin') + ':' + self.env['PATH']
        self.cfg = {'zwrt_router.network.opms_wan_mode': 'AUTO', 'zwrt_router.network.opms_wan_auto_mode': 'AUTO_DHCP', 'network.zte_wan.proto': 'dhcp', 'network.zte_wan.ifname': 'eth0', 'network.zte_wan.ipv6': '1', 'network.zte_wan6.proto': 'dhcpv6', 'network.zte_wan6.ifname': 'eth0', 'network.zte_wan6.ipv6': '1'}
        self.save()
        self.cmd('uci', '''#!/usr/bin/env python3
import json,os,pathlib,sys
p=pathlib.Path(os.environ['TEST_ROOT']); f=p/'config.json';d=json.loads(f.read_text()); a=[x for x in sys.argv[1:] if x!='-q']
if a[0]=='get':
 if a[1] not in d:sys.exit(1)
 print(d[a[1]])
else:
 with (p/'writes').open('a') as out:out.write(' '.join(a)+'\\n')
 if a[0]=='set':k,v=a[1].split('=',1);d[k]=v
 elif a[0]=='delete':d.pop(a[1],None)
 f.write_text(json.dumps(d))
''')
        self.cmd('ubus', '''#!/usr/bin/env python3
import os,pathlib,json,sys
p=pathlib.Path(os.environ['TEST_ROOT']);args=' '.join(sys.argv[1:])
with (p/'ubus').open('a') as f:f.write(args+'\\n')
if 'router_smart_wan_event' in args and not (p/'native-fail').exists():
 d=json.loads((p/'config.json').read_text())
 assert 'network.zte_wan.ifname' not in d and 'network.zte_wan6.ifname' not in d
 d.update({'zwrt_router.network.opms_wan_auto_mode':'AUTO_LTE_GATEWAY','network.zte_wan.proto':'rmnet','network.zte_wan6.proto':'rmnet','zwrt_router.tmp_router.dns':'219.141.136.10'})
 (p/'config.json').write_text(json.dumps(d))
''')
        self.cmd('flock', '#!/bin/sh\nexit 0\n')
        service=self.panel/'test-service'
        service.write_text('#!/bin/sh\ncase "$1" in\n running) [ -f "$TEST_ROOT/service-running" ];;\n start) [ ! -f "$TEST_ROOT/service-fail" ] || exit 1;touch "$TEST_ROOT/service-running";;\n stop) rm -f "$TEST_ROOT/service-running";;\nesac\n')
        service.chmod(0o700);self.env['USB_SERVICE']=str(service)
        self.cmd('ip', '''#!/bin/sh
case "$*" in
 *route*default*) if [ -f "$TEST_ROOT/route-ready" ]; then echo 'default via 10.0.0.1 dev rmnet_data0'; fi;;
esac
''')
        helper = self.panel / 'usb-cellular-route.sh'
        helper.write_text('#!/bin/sh\n[ ! -f "$TEST_ROOT/not-ready" ] || exit 1\ntouch "$TEST_ROOT/route-ready"\n')
        helper.chmod(0o755)

    def cmd(self, name, body):
        p = self.root / 'bin' / name
        p.write_text(body)
        p.chmod(0o755)

    def save(self):
        (self.root / 'config.json').write_text(json.dumps(self.cfg))

    def run_role(self, *args):
        return subprocess.run(['sh', str(SCRIPT), *args], env=self.env, capture_output=True, text=True, timeout=30)

    def test_fallback_retains_auto_and_cleans_both_families(self):
        r = self.run_role('to-cellular')
        self.assertEqual(r.returncode, 0, r.stderr)
        d = json.loads((self.root / 'config.json').read_text())
        self.assertEqual(d['zwrt_router.network.opms_wan_mode'], 'AUTO')
        self.assertEqual(d['zwrt_router.network.opms_wan_auto_mode'], 'AUTO_LTE_GATEWAY')
        for name in ('zte_wan', 'zte_wan6'):
            self.assertNotIn('network.'+name+'.ifname', d)
            self.assertEqual(d['network.'+name+'.proto'], 'rmnet')
        self.assertEqual(d['network.zte_wan.ipv6'], '0')
        self.assertEqual(d['network.zte_wan6.ipv6'], '1')
        self.assertNotIn('router_set_wan_mode', (self.root / 'ubus').read_text())

    def test_stock_sleep_never_redials_missing_cellular_routes(self):
        helper=self.panel/'panel-standby';helper.write_text('#!/bin/sh\n[ "$1" = blocked ]\n');helper.chmod(0o700)
        (self.panel/'usb-role').write_text('LAN\n')
        r=self.run_role('reconcile');self.assertEqual(r.returncode,0,r.stderr)
        self.assertFalse((self.root/'ubus').exists());self.assertFalse((self.root/'writes').exists());self.assertFalse((self.root/'route-ready').exists())

    def test_asleep_journal_suppresses_route_repair(self):
        (self.root/'tmp/u60-standby').mkdir();(self.root/'tmp/u60-standby/asleep').touch()
        r=self.run_role('reconcile');self.assertEqual(r.returncode,0,r.stderr)
        self.assertFalse((self.root/'ubus').exists());self.assertFalse((self.root/'writes').exists())

    def test_native_recovery_updates_dns_not_only_routes(self):
        self.assertEqual(self.run_role('to-cellular').returncode, 0)
        d=json.loads((self.root/'config.json').read_text())
        self.assertEqual(d['zwrt_router.tmp_router.dns'],'219.141.136.10')
        self.assertIn('notify_rj45_pull_out',(self.root/'ubus').read_text())
        self.assertNotIn('set zwrt_router.network.opms_wan_auto_mode=',(self.root/'writes').read_text())

    def test_native_failure_does_not_forge_cellular_state(self):
        (self.root/'native-fail').touch()
        self.assertNotEqual(self.run_role('to-cellular').returncode,0)
        self.assertNotEqual(self.run_role('to-cellular').returncode,0)
        self.assertEqual((self.root/'ubus').read_text().count('router_smart_wan_event'),1)
        d=json.loads((self.root/'config.json').read_text())
        self.assertEqual(d['zwrt_router.network.opms_wan_auto_mode'],'AUTO_DHCP')
        self.assertFalse((self.root/'route-ready').exists())

    def test_native_preserves_dhcp_flag_without_recovery_loop(self):
        self.assertEqual(self.run_role('to-cellular').returncode,0)
        (self.root/'proc/uptime').write_text('215.0 0')
        self.assertEqual(self.run_role('to-cellular').returncode,0)
        self.assertEqual((self.root/'ubus').read_text().count('router_smart_wan_event'),1)
        self.assertEqual(json.loads((self.root/'config.json').read_text())['network.zte_wan.ipv6'],'0')

    def test_rmnet_flag_only_does_not_renotify_pullout(self):
        self.cfg.update({'zwrt_router.network.opms_wan_auto_mode':'AUTO_LTE_GATEWAY','network.zte_wan.proto':'rmnet','network.zte_wan6.proto':'rmnet'})
        self.cfg.pop('network.zte_wan.ifname');self.cfg.pop('network.zte_wan6.ifname');self.save()
        self.assertEqual(self.run_role('to-cellular').returncode,0)
        self.assertNotIn('router_smart_wan_event',(self.root/'ubus').read_text())
        self.assertEqual(json.loads((self.root/'config.json').read_text())['network.zte_wan.ipv6'],'0')

    def test_no_route_is_not_success(self):
        (self.root / 'not-ready').touch()
        self.assertNotEqual(self.run_role('to-cellular').returncode, 0)

    def test_settled_restore_is_idempotent(self):
        self.assertEqual(self.run_role('to-cellular').returncode, 0)
        before=(self.root / 'writes').read_text()
        self.assertEqual(self.run_role('to-cellular').returncode, 0)
        self.assertEqual((self.root / 'writes').read_text(), before)

    def test_does_not_steal_manual_dhcp_mode(self):
        self.cfg['zwrt_router.network.opms_wan_mode']='DHCP';self.save()
        self.assertNotEqual(self.run_role('to-cellular').returncode, 0)
        self.assertFalse((self.root / 'writes').exists())

    def test_absent_adapter_never_shows_wan(self):
        (self.panel / 'usb-role').write_text('AUTO\n')
        r=json.loads(self.run_role('status').stdout)
        self.assertEqual(r['requested'],'AUTO')
        self.assertEqual(r['state'],'WAIT_ADAPTER')
        self.assertFalse(r['adapter'])
        self.assertEqual(r['badge'],'')

    def test_unknown_adapter_is_readonly_error(self):
        (self.root / 'sys/class/net/eth0').mkdir(parents=True)
        r=json.loads(self.run_role('status').stdout)
        self.assertEqual(r['state'],'UNSUPPORTED')
        self.assertEqual(r['badge'],'ERROR')
        self.assertFalse((self.root / 'writes').exists())

    def adapter(self, carrier='1'):
        net=self.root/'sys/class/net/eth0';net.mkdir(parents=True)
        dev=self.root/'sys/devices/usb/2-1/2-1:1.0';dev.mkdir(parents=True)
        driver=self.root/'sys/bus/usb/drivers/ax_usb_nic';driver.mkdir(parents=True)
        (dev/'driver').symlink_to(driver);(net/'device').symlink_to(dev)
        (dev.parent/'idVendor').write_text('0b95');(dev.parent/'idProduct').write_text('1790')
        (net/'carrier').write_text(carrier);(net/'ifindex').write_text('17')
        (self.root/'proc').mkdir(exist_ok=True);(self.root/'proc/uptime').write_text('200.00 0')
        self.cmd('ebtables','#!/bin/sh\nexit 0\n')
        self.cmd('udhcpc','#!/bin/sh\ntouch "$TEST_ROOT/probed"\nexit 1\n')
        return net

    def test_realtek_non_eth0_is_lan_only(self):
        net=self.adapter();net.rename(net.with_name('eth1'))
        dev=self.root/'sys/devices/usb/2-1/2-1:1.0'
        (dev/'driver').unlink();driver=self.root/'sys/bus/usb/drivers/r8152';driver.mkdir();(dev/'driver').symlink_to(driver)
        (dev.parent/'idVendor').write_text('0bda');(dev.parent/'idProduct').write_text('8153')
        (self.panel/'usb-role').write_text('LAN')
        result=json.loads(self.run_role('status').stdout)
        self.assertTrue(result['lan_supported']);self.assertFalse(result['wan_supported']);self.assertEqual(result['interface'],'eth1')
        self.assertNotEqual(self.run_role('set','AUTO').returncode,0)
        self.assertFalse((self.root/'writes').exists())
        self.cfg.update({'zwrt_router.network.opms_wan_mode':'PPP','network.zte_wan.proto':'rmnet','network.zte_wan6.proto':'rmnet','network.zte_wan.ipv6':'0'})
        self.cfg.pop('network.zte_wan.ifname');self.cfg.pop('network.zte_wan6.ifname');self.save()
        self.cmd('ip','''#!/usr/bin/env python3
import os,pathlib,sys
r=pathlib.Path(os.environ['TEST_ROOT']);a=sys.argv[1:]
if 'route' in a:print('default via 10.0.0.1 dev rmnet_data0')
if a[:3]==['link','set','dev'] and 'master' in a:(r/'sys/class/net'/a[3]/'master').symlink_to('../br-lan')
''')
        self.cmd('ebtables', '#!/bin/sh\n[ "$1" != -D ]\n')
        (self.panel/'network-profile').write_text('direct')
        for name in ['network-profile.sh','tailscale-lan.sh']:
            h=self.panel/name;h.write_text('#!/bin/sh\nexit 0\n');h.chmod(0o700)
        (self.root/'sys/class/net/br-lan').mkdir()
        self.assertEqual(self.run_role('reconcile').returncode,0)
        self.assertTrue((net.with_name('eth1')/'master').is_symlink())

    def test_multiple_adapters_and_unknown_drivers_are_rejected(self):
        net=self.adapter()
        other=net.with_name('eth1');other.mkdir();(other/'device').symlink_to((net/'device').resolve());(other/'ifindex').write_text('18')
        r=json.loads(self.run_role('status').stdout)
        self.assertFalse(r['lan_supported']);self.assertEqual(r['state'],'UNSUPPORTED')
        self.assertNotEqual(self.run_role('set','LAN').returncode,0)
        self.assertFalse((self.root/'writes').exists())

    def test_common_driver_admission_does_not_claim_wan(self):
        net=self.adapter();dev=(net/'device').resolve()
        for name in ['r8152','cdc_ether','cdc_ncm','aqc111','asix','ax88179_178a','unknown_driver']:
            (dev/'driver').unlink();d=self.root/'sys/bus/usb/drivers'/name;d.mkdir(exist_ok=True);(dev/'driver').symlink_to(d)
            r=json.loads(self.run_role('status').stdout)
            self.assertEqual(r['lan_supported'],name!='unknown_driver');self.assertFalse(r['wan_supported'])
        self.assertFalse((self.root/'writes').exists())

    def test_lan_switch_refused_with_live_upstream(self):
        self.adapter();r=self.run_role('set','LAN');self.assertNotEqual(r.returncode,0)
        self.assertFalse((self.panel/'usb-role').exists());self.assertFalse((self.root/'writes').exists())

    def test_role_allowlist(self):
        self.assertNotEqual(self.run_role('set','WAN;reboot').returncode,0)
        self.assertFalse((self.panel/'usb-role').exists())

    def test_set_starts_owner_without_synchronous_mode_change(self):
        self.adapter();r=self.run_role('set','AUTO');self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual((self.panel/'usb-role').read_text().strip(),'AUTO')
        self.assertFalse((self.root/'writes').exists())
        self.assertTrue((self.root/'service-running').exists())
        self.assertTrue((self.panel/'usb-managed').exists())

    def test_service_failure_restores_choice(self):
        self.adapter('0');(self.panel/'usb-role').write_text('LAN\n');(self.root/'service-fail').touch()
        result=self.run_role('set','AUTO');self.assertNotEqual(result.returncode,0);self.assertFalse(json.loads(result.stdout)['ok'])
        self.assertEqual((self.panel/'usb-role').read_text(),'LAN\n');self.assertFalse((self.panel/'usb-managed').exists())
        self.assertFalse((self.root/'service-running').exists())

    def test_status_exposes_saved_intent_without_owner(self):
        (self.panel/'usb-managed').touch();(self.panel/'usb-role').write_text('LAN\n')
        result=json.loads(self.run_role('status').stdout);self.assertEqual(result['state'],'SERVICE_DOWN');self.assertFalse(result['service_running'])

    def test_debounce_before_probe(self):
        self.adapter();(self.panel/'usb-role').write_text('AUTO')
        r=self.run_role('reconcile');self.assertEqual(r.returncode,0,r.stderr)
        self.assertFalse((self.root/'probed').exists())
        self.assertEqual((self.root/'tmp/u60-usb-role/attachment').read_text().strip(),'17')

    def test_dhcp_timeout_never_becomes_lan(self):
        self.adapter();(self.panel/'usb-role').write_text('AUTO')
        self.run_role('reconcile');(self.root/'proc/uptime').write_text('210.0 0')
        self.run_role('reconcile');self.assertTrue((self.root/'probed').exists())
        d=json.loads((self.root/'config.json').read_text());self.assertEqual(d['zwrt_router.network.opms_wan_mode'],'AUTO')
        self.assertEqual((self.root/'tmp/u60-usb-role/phase').read_text().strip(),'NO_UPSTREAM')

    def test_carrier_down_clears_stale_attachment(self):
        self.adapter('0');(self.panel/'usb-role').write_text('AUTO')
        run=self.root/'tmp/u60-usb-role';run.mkdir();(run/'attachment').write_text('16');(run/'accepted').write_text('10')
        self.run_role('reconcile');self.assertFalse((run/'attachment').exists());self.assertFalse((run/'accepted').exists())
        self.assertTrue((self.root/'route-ready').exists())

    def test_explicitly_down_ipv6_is_reenabled_by_netifd(self):
        (self.root/'proc').mkdir(exist_ok=True);(self.root/'proc/uptime').write_text('200.0 0')
        self.cmd('jsonfilter', '#!/bin/sh\necho false\n')
        self.assertEqual(self.run_role('to-cellular').returncode,0)
        calls=(self.root/'ubus').read_text()
        self.assertIn('network.interface.zte_wan up',calls)
        # Force only IPv6 to lack a route, with IPv4 already working.
        self.cmd('ip', '#!/bin/sh\n[ "$1" != -4 ] || echo "default via 10.0.0.1 dev rmnet_data0"\n')
        self.run_role('to-cellular')
        self.assertIn('network.interface.zte_wan6 up',(self.root/'ubus').read_text())

    def idle_fixture(self):
        (self.panel/'usb-role').write_text('LAN')
        run=self.root/'tmp/u60-usb-role';run.mkdir(exist_ok=True)
        (run/'phase').write_text('LAN');(run/'settle_until').write_text('100')
        net=self.root/'proc/net';net.mkdir()
        (net/'route').write_text('Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT\nrmnet_data0 00000000 0100000A 0003 0 0 256 00000000 0 0 0\n')
        (net/'ipv6_route').write_text('00000000000000000000000000000000 00 00000000000000000000000000000000 00 fe800000000000000000000000000001 00000400 00000000 00000000 00000003 rmnet_data0\n')
        return run,net

    def test_empty_port_stable_dual_stack_can_skip_expensive_work(self):
        self.idle_fixture()
        self.assertEqual(self.run_role('idle-ready','LAN').returncode,0)
        self.assertFalse((self.root/'ubus').exists())
        self.assertFalse((self.root/'writes').exists())

    def test_idle_does_not_delay_role_change_attachment_or_recovery(self):
        run,net=self.idle_fixture()
        self.assertNotEqual(self.run_role('idle-ready','AUTO').returncode,0)
        (run/'phase').write_text('RESTORING')
        self.assertNotEqual(self.run_role('idle-ready','LAN').returncode,0)
        (run/'phase').write_text('LAN');(run/'settle_until').write_text('300')
        self.assertNotEqual(self.run_role('idle-ready','LAN').returncode,0)
        (run/'settle_until').write_text('100');self.adapter('0')
        self.assertNotEqual(self.run_role('idle-ready','LAN').returncode,0)

    def test_idle_requires_both_real_cellular_default_routes(self):
        run,net=self.idle_fixture()
        original=(net/'route').read_text();(net/'route').write_text('')
        self.assertNotEqual(self.run_role('idle-ready','LAN').returncode,0)
        (net/'route').write_text(original.replace('rmnet_data0','eth0'))
        self.assertNotEqual(self.run_role('idle-ready','LAN').returncode,0)
        (net/'route').write_text(original);(net/'ipv6_route').write_text('')
        self.assertNotEqual(self.run_role('idle-ready','LAN').returncode,0)


if __name__=='__main__':
    unittest.main()
