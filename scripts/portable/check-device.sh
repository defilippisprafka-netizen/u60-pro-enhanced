#!/bin/sh
# Read-only hardware/firmware prerequisites; no installation or credentials.
set -eu
cd "$(dirname "$0")"
fail() { echo "$1" >&2;exit 1; }
[ "$(id -u)" = 0 ] || fail 'Root ADB shell required'
FW=$(ubus -t 5 call zwrt_zte_mdm.api get_zwrt_common_info '{}' | jsonfilter -e '@.wa_inner_version')
[ "$FW" = BD_FLYMODEMMU5250V1.0.0B28 ] || fail 'Only the verified mainland B28 firmware is supported'
[ "$(uname -m)" = aarch64 ] && [ "$(uname -r)" = 5.15.185-perf ] || fail 'Kernel or architecture mismatch'
sha256sum -c FACTORY-SHA256SUMS >/dev/null || fail 'Factory API/font/library fingerprint mismatch'
for name in ubus uci curl jsonfilter flock ip iptables ip6tables ebtables hostapd_cli start-stop-daemon;do
 command -v "$name" >/dev/null || fail "Missing required command: $name"
done
[ -c /dev/dri/card0 ] && [ -c /dev/net/tun ] || fail 'DRM or TUN device missing'
[ "$(cat /sys/class/input/event0/device/name)" = pmic_pwrkey ] || fail 'Power key layout mismatch'
[ "$(cat /sys/class/input/event3/device/name)" = sitronix_ts_i2c ] || fail 'Touch input layout mismatch'
[ -x /etc/init.d/zte_topsw_devui ] && [ -s /etc/rc.local ] || fail 'Factory UI/boot file missing'
[ "$(cat /sys/class/power_supply/usb/online)" = 1 ] || fail 'Keep a USB cable connected for installation'
[ ! -e /sys/class/net/eth0 ] || fail 'Remove the USB Ethernet adapter before installation'
FREE=$(df -k /data | awk 'END {print $(NF-2)}')
case "$FREE" in ''|*[!0-9]*) fail 'Cannot read available data space';; esac
[ "$FREE" -ge 400000 ] || fail 'At least 400 MB free in /data required'
echo "PASS: live B28 hardware and stock ABI prerequisites"
