#!/bin/sh
# Narrow netifd repair from live modem data; no global mode or firewall reset.
# IPv4 and IPv6 are independent: an IPv6 failure must not erase IPv4.
set -u
BASE=${U60_USB_TEST_ROOT:-}
UCI=${USB_UCI:-uci}
IP=${USB_IP:-ip}
PROTO_LIB=${USB_PROTO_LIB:-/lib/netifd/netifd-proto.sh}
FUNCTIONS_LIB=${USB_FUNCTIONS_LIB:-/lib/functions.sh}
family=${1:-4}
case "$family" in 4|6) ;; *) exit 2;; esac
mode=$("$UCI" -q get zwrt_router.network.opms_wan_mode)
case "$mode" in
 PPP) ;;
 AUTO) [ "$("$UCI" -q get zwrt_router.network.opms_wan_auto_mode)" = AUTO_LTE_GATEWAY ] || exit 2;;
 *) exit 2;;
esac
cfg=zte_wan
[ "$family" = 4 ] || cfg=zte_wan6
[ "$("$UCI" -q get network.$cfg.proto)" = rmnet ] || exit 2
FILE="$BASE/tmp/ipv${family}config1"
field() { sed -n "s/^export $1=\"\([^\"]*\)\"$/\1/p" "$FILE"; }
cell_if=$(field IFNAME)
case "$cell_if" in rmnet_data[0-9]|rmnet_data[0-9][0-9]) ;; *) exit 2;; esac
[ -d "$BASE/sys/class/net/$cell_if" ] || exit 2
if [ "$family" = 4 ]; then
 cell_ip=$(field PUBLIC_IP); cell_mask=$(field NETMASK)
 cell_gw=$(field GATEWAY); cell_mtu=$(field IPV4MTU); cell_dns=$(field DNSSERVERS)
 for value in "$cell_ip" "$cell_mask" "$cell_gw"; do
  case "$value" in ''|*[!0-9.]*) exit 2;; esac
 done
else
 cell_ip=$(field PUBLIC_IP6); cell_mask=$(field NETMASK6)
 cell_gw=$(field GATEWAY6); cell_mtu=$(field IPV6MTU); cell_dns=$(field DNSSERVERS6)
 for value in "$cell_ip" "$cell_gw"; do
  case "$value" in ''|*[!0-9a-fA-F:]*) exit 2;; esac
 done
 case "$cell_mask" in ''|*[!0-9]*) exit 2;; esac
 [ "$cell_mask" -le 128 ] || exit 2
fi
case "$cell_mtu" in ''|*[!0-9]*) exit 2;; esac
# Firmware shell libraries intentionally read optional unset variables.
set +u
. "$FUNCTIONS_LIB"
. "$PROTO_LIB"
proto_init_update "$cell_if" 1 1
if [ "$family" = 4 ]; then
 proto_add_ipv4_address "$cell_ip" "$cell_mask"
 proto_add_ipv4_route 0.0.0.0 0 "$cell_gw" "$cell_ip" 256 "$cell_mtu"
else
 proto_add_ipv6_address "$cell_ip/$cell_mask" '' '' '' 1
 proto_add_ipv6_route :: 0 "$cell_gw" '' '' '' '' "$cell_mtu"
fi
for server in $cell_dns; do
 case "$server" in ''|*[!0-9a-fA-F:.]*) ;; *) proto_add_dns_server "$server";; esac
done
proto_send_update "$cfg" || exit 1
# B27 teardown can delete the kernel IPv6 default behind netifd's back.
# An identical proto update then leaves netifd's cached route marked present.
# Reapply only a missing route from the same validated modem snapshot.
if [ "$family" = 6 ] && ! "$IP" -6 route show default | grep -q " dev $cell_if "; then
 "$IP" -6 route replace default via "$cell_gw" dev "$cell_if" metric 1024 mtu "$cell_mtu" || exit 1
fi
