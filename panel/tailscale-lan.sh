#!/bin/sh
# Only bridge LAN clients to Tailnet IPv4 peers. Never changes the Internet exit.
set -u
umask 077
BASE=${U60_TSLAN_TEST_ROOT:-}
case "$BASE" in ''|/*) ;; *) exit 2;; esac
ROOT="$BASE/data/u60-panel"
STATE="$ROOT/tailscale-lan"
IPT=${TSLAN_IPTABLES:-iptables}
IP=${TSLAN_IP:-ip}
NET="$BASE/sys/class/net/tailscale0"
LOCK="$BASE/tmp/u60-tailscale-lan.lock"
WATCH="$BASE/tmp/u60-tailscale-lan-watch.lock"
command=${1:-status}
case "$command" in status|on|off|reconcile|watch) ;; *) exit 2;; esac
configured() { [ "$(cat "$STATE" 2>/dev/null)" = 1 ]; }
lan() {
 CIDR=$("$IP" -4 -o addr show dev br-lan 2>/dev/null | awk '$3 == "inet" {print $4; exit}')
 case "$CIDR" in ''|*[!0-9./]*|*/*/*) return 1;; */*) return 0;; *) return 1;; esac
}
rule() { table=$1; shift; "$IPT" -w 2 -t "$table" "$@" >/dev/null 2>&1; }
ensure() { table=$1; chain=$2; shift 2; rule "$table" -C "$chain" "$@" || rule "$table" -A "$chain" "$@"; }
remove() {
 while rule filter -D FORWARD -i br-lan -d 100.64.0.0/10 ! -o tailscale0 -j REJECT --reject-with icmp-net-unreachable; do :; done
 while rule filter -D FORWARD -i br-lan -d 100.64.0.0/10 -j U60_TS_LAN; do :; done
 while rule nat -D POSTROUTING -o tailscale0 -d 100.64.0.0/10 -j U60_TS_SNAT; do :; done
 rule filter -F U60_TS_LAN || :; rule filter -X U60_TS_LAN || :
 rule nat -F U60_TS_SNAT || :; rule nat -X U60_TS_SNAT || :
 ! rule filter -C FORWARD -i br-lan -d 100.64.0.0/10 ! -o tailscale0 -j REJECT --reject-with icmp-net-unreachable &&
 ! rule filter -C FORWARD -i br-lan -d 100.64.0.0/10 -j U60_TS_LAN && ! rule nat -C POSTROUTING -o tailscale0 -d 100.64.0.0/10 -j U60_TS_SNAT
}
ready() {
 lan || return 1
 rule filter -C FORWARD -i br-lan -d 100.64.0.0/10 ! -o tailscale0 -j REJECT --reject-with icmp-net-unreachable &&
 rule filter -C FORWARD -i br-lan -d 100.64.0.0/10 -j U60_TS_LAN &&
 rule filter -C U60_TS_LAN -s "$CIDR" -o tailscale0 -j ACCEPT &&
 rule filter -C U60_TS_LAN -j REJECT --reject-with icmp-net-unreachable &&
 rule nat -C POSTROUTING -o tailscale0 -d 100.64.0.0/10 -j U60_TS_SNAT &&
 rule nat -C U60_TS_SNAT -s "$CIDR" -j MASQUERADE
}
reconcile() {
 configured || { remove; return $?; }
 ready && return 0
 lan || return 1
 rule filter -C FORWARD -i br-lan -d 100.64.0.0/10 ! -o tailscale0 -j REJECT --reject-with icmp-net-unreachable || rule filter -I FORWARD 1 -i br-lan -d 100.64.0.0/10 ! -o tailscale0 -j REJECT --reject-with icmp-net-unreachable || return 1
 # Keep a terminal rejection while rebuilding: no Tailnet leak to cellular.
 rule filter -N U60_TS_LAN || :
 ensure filter U60_TS_LAN -j REJECT --reject-with icmp-net-unreachable || return 1
 rule filter -C FORWARD -i br-lan -d 100.64.0.0/10 -j U60_TS_LAN || rule filter -I FORWARD 1 -i br-lan -d 100.64.0.0/10 -j U60_TS_LAN || return 1
 rule nat -N U60_TS_SNAT || :
 rule nat -F U60_TS_SNAT || return 1
 ensure nat U60_TS_SNAT -s "$CIDR" -j MASQUERADE || return 1
 rule nat -C POSTROUTING -o tailscale0 -d 100.64.0.0/10 -j U60_TS_SNAT || rule nat -I POSTROUTING 1 -o tailscale0 -d 100.64.0.0/10 -j U60_TS_SNAT || return 1
 # Remove stale LAN accepts, then place the current one before the rejection.
 rule filter -F U60_TS_LAN || return 1
 ensure filter U60_TS_LAN -j REJECT --reject-with icmp-net-unreachable || return 1
 rule filter -I U60_TS_LAN 1 -s "$CIDR" -o tailscale0 -j ACCEPT || return 1
 ready
}
emit() {
 enabled=false;active=false
 configured && enabled=true
 [ "$enabled" != true ] || { [ -e "$NET" ] && ready && active=true; }
 printf '{"ok":%s,"enabled":%s,"active":%s,"scope":"tailnet_ipv4","message":"%s"}\n' "$1" "$enabled" "$active" "$2"
}
[ "$command" != status ] || { emit true 'LAN Tailnet gateway status'; exit 0; }
if [ "$command" = watch ]; then
 exec 8>"$WATCH"; flock -n 8 || exit 0
 trap 'exit 0' INT TERM HUP
 while :; do
  if [ ! -f "$BASE/tmp/u60-standby/active" ];then (exec 8>&-; sh "$0" reconcile >/dev/null 2>&1);fi
  sleep 10 8>&-
 done
fi
exec 9>"$LOCK"; flock -n 9 || { emit false 'Another gateway operation is active'; exit 1; }
if [ "$command" = on ]; then
 [ -e "$NET" ] && lan && [ "$(cat "$BASE/proc/sys/net/ipv4/ip_forward" 2>/dev/null)" = 1 ] || { emit false 'TUN, LAN or IP forwarding is not ready'; exit 1; }
 old=0; configured && old=1
 printf '1\n' > "$STATE.next" && mv "$STATE.next" "$STATE" || exit 1
 if ! reconcile; then printf '%s\n' "$old" > "$STATE"; [ "$old" != 0 ] || remove; emit false 'Gateway rules failed; previous setting restored'; exit 1; fi
elif [ "$command" = off ]; then
 printf '0\n' > "$STATE.next" && mv "$STATE.next" "$STATE" || exit 1
 remove || { emit false 'Gateway cleanup failed'; exit 1; }
else
 reconcile || { emit false 'Gateway reconciliation failed'; exit 1; }
fi
emit true 'Gateway setting and scoped rules verified'
