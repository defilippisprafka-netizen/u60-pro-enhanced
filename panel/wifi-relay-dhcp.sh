#!/bin/sh
set -u
umask 077
ROOT=/data/u60-panel
RUN=/tmp/u60-wifi-relay
exec 9>"$RUN/lease.lock";flock 9
[ "${interface:-}" = u60sta ] || exit 1
[ -f "$ROOT/relay-private/enabled" ] && [ -f "$RUN/enabled" ] || exit 1
[ -n "${U60_RELAY_GENERATION:-}" ] && [ "$U60_RELAY_GENERATION" = "$(cat "$RUN/dhcp-generation" 2>/dev/null)" ] || exit 1
[ "${U60_RELAY_IFINDEX:-}" = "$(cat /sys/class/net/u60sta/ifindex 2>/dev/null)" ] || exit 1
case "${1:-}" in
 deconfig|nak|leasefail)
  ip -4 route del default dev u60sta metric 50 2>/dev/null || :
  if [ -f "$RUN/ipv6-block" ];then ip -6 route del unreachable default metric 50 2>/dev/null || :;rm -f "$RUN/ipv6-block";fi
  ip -4 addr flush dev u60sta scope global
  rm -f "$RUN/lease-ready";echo CELLULAR > "$RUN/phase";exit 0;;
 bound|renew) ;;
 *) exit 0;;
esac
# Only scalar IPv4 values enter JSON or command arguments; never eval DHCP text.
gateway=${router:-};gateway=${gateway%% *}
for value in "${ip:-}" "${subnet:-}" "$gateway";do case "$value" in ''|*[!0-9.]*) exit 1;;esac;done
lan=$(uci -q get network.lan.ipaddr);mask=$(uci -q get network.lan.netmask)
for value in "$lan" "$mask";do case "$value" in ''|*[!0-9.]*) exit 1;;esac;done
reply=$(printf '{"ip":"%s","subnet":"%s","router":"%s","lan":"%s","lan_mask":"%s"}' "$ip" "$subnet" "$gateway" "$lan" "$mask" | "$ROOT/panel-relay" lease)
if [ "$(printf '%s' "$reply" | jsonfilter -e '@.ok')" != true ];then
 ip -4 route del default dev u60sta metric 50 2>/dev/null || :
 if [ -f "$RUN/ipv6-block" ];then ip -6 route del unreachable default metric 50 2>/dev/null || :;rm -f "$RUN/ipv6-block";fi
 rm -f "$RUN/lease-ready";ip -4 addr flush dev u60sta scope global
 state=ERROR;[ "$(printf '%s' "$reply" | jsonfilter -e '@.message')" != SUBNET_CONFLICT ] || state=CONFLICT
 echo "$state" > "$RUN/phase";exit 1
fi
prefix=$(printf '%s' "$reply" | jsonfilter -e '@.prefix');case "$prefix" in ''|*[!0-9]*) exit 1;;esac
fail_lease() {
 ip -4 route del default dev u60sta metric 50 2>/dev/null || :
 if [ -f "$RUN/ipv6-block" ];then ip -6 route del unreachable default metric 50 2>/dev/null || :;rm -f "$RUN/ipv6-block";fi
 rm -f "$RUN/lease-ready";echo ERROR > "$RUN/phase";exit 1
}
if ! ip -4 addr show dev u60sta | grep -F -q "inet $ip/$prefix ";then
 ip -4 addr flush dev u60sta scope global
 ip -4 addr add "$ip/$prefix" dev u60sta || fail_lease
fi
# Install restricted forwarding before publishing the new Internet default.
"$ROOT/wifi-relay.sh" rules || fail_lease
if [ ! -f "$RUN/ipv6-block" ] && ip -6 route show default | grep -q "metric 50 ";then exit 1;fi
ip -6 route replace unreachable default metric 50 || fail_lease
touch "$RUN/ipv6-block"
if ! ip -4 route replace default via "$gateway" dev u60sta metric 50;then ip -6 route del unreachable default metric 50;rm -f "$RUN/ipv6-block";exit 1;fi
touch "$RUN/lease-ready";echo CONNECTED > "$RUN/phase"
