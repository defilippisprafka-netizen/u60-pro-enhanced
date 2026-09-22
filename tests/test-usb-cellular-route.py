#!/usr/bin/env python3
import os
import pathlib
import subprocess
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).parents[1] / 'panel/usb-cellular-route.sh'


class CellularRouteTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        (self.root / 'tmp').mkdir()
        (self.root / 'sys/class/net/rmnet_data0').mkdir(parents=True)
        self.log = self.root / 'calls'
        self.env = dict(os.environ, U60_USB_TEST_ROOT=str(self.root), CALL_LOG=str(self.log), MODE='AUTO', AUTO_MODE='AUTO_LTE_GATEWAY', PROTO='rmnet')
        uci = self.root / 'uci'
        uci.write_text('''#!/bin/sh
case "$*" in
 *opms_wan_auto_mode) echo "$AUTO_MODE";;
 *opms_wan_mode) echo "$MODE";;
 *.proto) echo "$PROTO";;
esac
''')
        uci.chmod(0o755)
        self.env['USB_UCI'] = str(uci)
        ip = self.root / 'ip'
        ip.write_text('#!/bin/sh\necho "ip $*" >> "$CALL_LOG"\nif [ "$2 $3 $4" = "route show default" ] && [ "${HAS_ROUTE:-0}" = 1 ]; then echo "default via fe80::1 dev rmnet_data0 metric 1024"; fi\n')
        ip.chmod(0o755)
        self.env['USB_IP'] = str(ip)
        (self.root / 'functions').write_text(': "$OPTIONAL_UNSET_VARIABLE"\n')
        self.env['USB_FUNCTIONS_LIB'] = str(self.root / 'functions')
        names = ['proto_init_update', 'proto_add_ipv4_address', 'proto_add_ipv4_route', 'proto_add_ipv6_address', 'proto_add_ipv6_route', 'proto_add_dns_server', 'proto_send_update']
        (self.root / 'proto').write_text('\n'.join(f'{n}() {{ echo "{n} $*" >> "$CALL_LOG"; }}' for n in names))
        self.env['USB_PROTO_LIB'] = str(self.root / 'proto')
        self.v4 = 'export IFNAME="rmnet_data0"\nexport PUBLIC_IP="10.1.2.3"\nexport NETMASK="255.255.255.248"\nexport GATEWAY="10.1.2.1"\nexport IPV4MTU="1500"\nexport DNSSERVERS="1.1.1.1"\n'
        self.v6 = 'export IFNAME="rmnet_data0"\nexport PUBLIC_IP6="2001:db8::1"\nexport NETMASK6="64"\nexport GATEWAY6="fe80::1"\nexport IPV6MTU="1500"\nexport DNSSERVERS6="2001:db8::53"\n'
        (self.root / 'tmp/ipv4config1').write_text(self.v4)
        (self.root / 'tmp/ipv6config1').write_text(self.v6)

    def run_script(self, family='4'):
        return subprocess.run(['sh', str(SCRIPT), family], env=self.env, capture_output=True, text=True, timeout=15)

    def test_live_ipv4_fields_are_used(self):
        self.assertEqual(self.run_script().returncode, 0)
        self.assertIn('proto_add_ipv4_route 0.0.0.0 0 10.1.2.1 10.1.2.3 256 1500', self.log.read_text())
        self.assertIn('proto_send_update zte_wan\n', self.log.read_text())

    def test_ipv6_uses_separate_interface(self):
        self.assertEqual(self.run_script('6').returncode, 0)
        self.assertIn('proto_add_ipv6_address 2001:db8::1/64', self.log.read_text())
        self.assertIn('proto_send_update zte_wan6\n', self.log.read_text())
        self.assertNotIn('proto_add_ipv4', self.log.read_text())

    def test_missing_kernel_ipv6_default_is_repaired(self):
        self.assertEqual(self.run_script('6').returncode,0)
        self.assertIn('ip -6 route replace default via fe80::1 dev rmnet_data0 metric 1024 mtu 1500',self.log.read_text())

    def test_existing_kernel_ipv6_default_is_not_replaced(self):
        self.env['HAS_ROUTE']='1'
        self.assertEqual(self.run_script('6').returncode,0)
        self.assertNotIn('route replace',self.log.read_text())

    def test_does_not_overwrite_active_wan(self):
        for mode, auto in [('DHCP', 'AUTO_LTE_GATEWAY'), ('AUTO', 'AUTO_DHCP'), ('AUTO', 'AUTO_PPPOE')]:
            self.env.update(MODE=mode, AUTO_MODE=auto)
            self.assertNotEqual(self.run_script().returncode, 0)
            self.assertFalse(self.log.exists())

    def test_ppp_restore_allowed(self):
        self.env['MODE'] = 'PPP'
        self.assertEqual(self.run_script().returncode, 0)

    def test_unexpected_interface_refused(self):
        (self.root / 'tmp/ipv4config1').write_text(self.v4.replace('rmnet_data0', 'eth0'))
        self.assertNotEqual(self.run_script().returncode, 0)
        self.assertFalse(self.log.exists())

    def test_config_is_data_not_shell(self):
        evil = self.root / 'executed'
        (self.root / 'tmp/ipv4config1').write_text(self.v4.replace('10.1.2.1', f'$(touch {evil})'))
        self.assertNotEqual(self.run_script().returncode, 0)
        self.assertFalse(evil.exists())
        self.assertFalse(self.log.exists())

    def test_notify_rejection_is_reported(self):
        with (self.root / 'proto').open('a') as f:
            f.write('\nproto_send_update() { return 1; }\n')
        self.assertNotEqual(self.run_script().returncode, 0)


if __name__ == '__main__':
    unittest.main()
