#!/bin/sh
# Read-only admission checks precede the factory's tested DHCP adoption callback.
set -u
umask 077
BASE=${U60_USB_TEST_ROOT:-}
ROOT="$BASE/data/u60-panel"
RUN="$BASE/tmp/u60-usb-role"
NET="$BASE/sys/class/net"
UCI=${USB_UCI:-uci}
IPCALC=${USB_IPCALC:-/bin/ipcalc.sh}
VENDOR=${USB_VENDOR_DHCP:-/sbin/zte_dhcp_discovery.sh}
case "${1:-}" in bound|renew) ;; *) exit 0;; esac
[ "$(cat "$ROOT/usb-role" 2>/dev/null)" = AUTO ] || exit 2
[ "${interface:-}" = eth0 ] || exit 2
[ "$(cat "$NET/eth0/carrier" 2>/dev/null)" = 1 ] || exit 2
[ -n "${U60_USB_ATTACHMENT:-}" ] && [ "$U60_USB_ATTACHMENT" = "$(cat "$NET/eth0/ifindex")" ] || exit 2
[ "$(cat "$RUN/attachment" 2>/dev/null)" = "$U60_USB_ATTACHMENT" ] || exit 2
[ "$("$UCI" -q get zwrt_router.network.opms_wan_mode)" = AUTO ] || exit 2
master=$(readlink -f "$NET/eth0/master" 2>/dev/null || true)
[ "${master##*/}" != br-lan ] || exit 2
# No eval/source of DHCP or ipcalc output. Validate every address as data.
valid4() {
 case "$1" in ''|*[!0-9.]*) return 1;; esac
 printf '%s\n' "$1" | awk -F. 'NF!=4{exit 1} {for(i=1;i<=4;i++)if($i!~/^[0-9]+$/||$i>255)exit 1}'
}
set -- ${router:-}
gateway=${1:-}
for value in "${ip:-}" "${subnet:-}" "$gateway"; do valid4 "$value" || exit 2; done
if [ -z "${broadcast:-}" ]; then
 broadcast=$("$IPCALC" "$ip" "$subnet" | sed -n 's/^BROADCAST=//p')
 export broadcast
fi
valid4 "$broadcast" || exit 2
case "$ip" in 0.*|127.*|169.254.*) exit 2;; esac
lan=$("$UCI" -q get network.lan.ipaddr) || exit 2
mask=$("$UCI" -q get network.lan.netmask) || exit 2
valid4 "$lan" && valid4 "$mask" || exit 2
network() { "$IPCALC" "$1" "$2" | sed -n 's/^NETWORK=//p'; }
a=$(network "$lan" "$mask"); b=$(network "$ip" "$mask")
c=$(network "$lan" "$subnet"); d=$(network "$ip" "$subnet")
[ -n "$a" ] && [ -n "$b" ] && [ -n "$c" ] && [ -n "$d" ] || exit 2
if [ "$a" = "$b" ] || [ "$c" = "$d" ]; then
 printf 'CONFLICT\n' > "$RUN/phase"
 exit 2
fi
[ "$(network "$gateway" "$subnet")" = "$d" ] || exit 2
# Recheck immediately before changing the interface, including request reversal.
[ "$(cat "$ROOT/usb-role")" = AUTO ] && [ "$(cat "$NET/eth0/ifindex")" = "$U60_USB_ATTACHMENT" ] && [ "$(cat "$NET/eth0/carrier")" = 1 ] || exit 2
"$VENDOR" bound >/dev/null 2>&1 || exit 1
cut -d . -f 1 "$BASE/proc/uptime" > "$RUN/accepted"
