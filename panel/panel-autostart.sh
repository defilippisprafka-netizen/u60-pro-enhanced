#!/bin/sh
# Preserve the factory boot barrier, then hand the display to the custom UI.
ROOT="${ROOT:-/data/u60-panel}"
if [ -x "$ROOT/usb-role.sh" ]; then "$ROOT/usb-role.sh" prepare >/dev/null 2>&1 || true; fi
n=0
while [ "$n" -lt 120 ]; do
	if [ -e /tmp/zte_boot_done ] && pidof zte_topsw_devui >/dev/null 2>&1; then
		break
	fi
	sleep 0.25
	n=$((n + 1))
done
[ -e /tmp/zte_boot_done ] && pidof zte_topsw_devui >/dev/null 2>&1 || exit 1
sleep 1
[ ! -x /etc/init.d/u60-standby ] || /etc/init.d/u60-standby start >/dev/null 2>&1
if [ -x "$ROOT/usb-role.sh" ] && [ -x /etc/init.d/u60-usb-role ]; then /etc/init.d/u60-usb-role start >/dev/null 2>&1 || true; fi
if [ -x "$ROOT/wifi-band.sh" ]; then "$ROOT/wifi-band.sh" watch >/dev/null 2>&1 & fi
if [ -x "$ROOT/panel-maintenance.sh" ]; then "$ROOT/panel-maintenance.sh" watch >/dev/null 2>&1 & fi
if [ -x "$ROOT/tailscale-lan.sh" ]; then "$ROOT/tailscale-lan.sh" watch >/dev/null 2>&1 & fi
[ ! -x /etc/init.d/u60-wifi-relay ] || /etc/init.d/u60-wifi-relay start >/dev/null 2>&1
"$ROOT/panel-watch.sh" >/dev/null 2>&1 &
exec "$ROOT/panel-run.sh"
