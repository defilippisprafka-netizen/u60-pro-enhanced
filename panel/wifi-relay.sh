#!/bin/sh
# Own only u60sta, its lease, and U60_RELAY_* rules. Never rewrites stock WAN.
set -u
umask 077
ROOT=/data/u60-panel
RUN=/tmp/u60-wifi-relay
PRIVATE=$ROOT/relay-private
CTRL=$RUN/ctrl
mkdir -p "$RUN" "$PRIVATE";chmod 700 "$RUN" "$PRIVATE"
phase() { printf '%s\n' "$1" > "$RUN/phase.next" && mv "$RUN/phase.next" "$RUN/phase"; }
wpa() { wpa_cli -p "$CTRL" -i u60sta "$@" 2>/dev/null; }
setting() { uci -q get "$1" 2>/dev/null; }
ap_state() {
 (
  hostapd_cli -p /data/vendor/wifi/hostapd -i "$1" status 2>/dev/null & ap_pid=$!
  (sleep 3;kill "$ap_pid" 2>/dev/null) 8>&- 9>&- >/dev/null 2>&1 & timer_pid=$!
  wait "$ap_pid";result=$?
  kill "$timer_pid" 2>/dev/null || :;wait "$timer_pid" 2>/dev/null || :
  exit "$result"
 ) | sed -n 's/^state=//p'
}
allowed() {
 [ "$(cat "$ROOT/usb-role" 2>/dev/null)" = LAN ] &&
 [ "$(setting zwrt_router.network.opms_wan_mode)" = PPP ] &&
 [ "$(setting wireless.zte_mbb.wifi_onoff)" = 1 ] &&
 [ "$(ap_state wlan2)" = ENABLED ] && [ "$(ap_state wlan0)" != ENABLED ] &&
 [ "$(ap_state wlan1)" != ENABLED ] && [ "$(ap_state wlan3)" != ENABLED ]
}
prepare() {
 allowed || return 1
 if [ ! -e /sys/class/net/u60sta ]; then
  phy=$(basename "$(readlink -f /sys/class/net/wlan2/phy80211)")
  case "$phy" in phy[0-9]*) ;; *) return 1;; esac
  iw phy "$phy" interface add u60sta type managed || return 1
  # Device-specific local MAC. Never share the driver's placeholder across routers.
  mac=$(cat /sys/class/net/wlan2/address);mac="02:${mac#*:}"
  ip link set dev u60sta address "$mac" || return 1
 fi
 firewall || return 1
 ip link set dev u60sta up || return 1
 if [ "$(wpa ping)" != PONG ]; then
  mkdir -p "$CTRL";chmod 700 "$CTRL"
  if [ ! -f "$PRIVATE/wpa.conf" ]; then
   printf 'ctrl_interface=%s\nupdate_config=1\n' "$CTRL" > "$PRIVATE/wpa.conf"
   chmod 600 "$PRIVATE/wpa.conf"
  fi
  wpa_supplicant -B -D nl80211 -i u60sta -c "$PRIVATE/wpa.conf" -P "$RUN/supp.pid" -f /dev/null -qq >/dev/null 2>&1 || return 1
  n=0;while [ "$(wpa ping)" != PONG ];do n=$((n+1));[ "$n" -lt 20 ] || return 1;sleep .1;done
  [ -f "$PRIVATE/enabled" ] || wpa disconnect >/dev/null
 fi
}
ipt() { iptables -w 2 "$@" >/dev/null 2>&1; }
ensure() { table=$1;chain=$2;shift 2;ipt -t "$table" -C "$chain" "$@" || ipt -t "$table" -A "$chain" "$@"; }
firewall() {
 for chain in U60_RELAY_IN U60_RELAY_FWD;do ipt -N "$chain" || :;done
 ipt -t nat -N U60_RELAY_NAT || :
 ensure filter U60_RELAY_IN -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT &&
 ensure filter U60_RELAY_IN -p udp --sport 67 --dport 68 -j ACCEPT &&
 ensure filter U60_RELAY_IN -j DROP || return 1
 # Keep Clash's QUIC fallback even though this jump is before stock forwarding.
 if [ "$(cat "$ROOT/network-profile" 2>/dev/null)" = clash ]; then
  ipt -C U60_RELAY_FWD -i br-lan -o u60sta -p udp --dport 443 -j REJECT || ipt -I U60_RELAY_FWD 1 -i br-lan -o u60sta -p udp --dport 443 -j REJECT || return 1
 else
  while ipt -D U60_RELAY_FWD -i br-lan -o u60sta -p udp --dport 443 -j REJECT;do :;done
 fi
 ensure filter U60_RELAY_FWD -i br-lan -o u60sta -d 100.64.0.0/10 -j REJECT &&
 ensure filter U60_RELAY_FWD -i br-lan -o u60sta -j ACCEPT &&
 ensure filter U60_RELAY_FWD -i u60sta -o br-lan -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT &&
 ensure filter U60_RELAY_FWD -i u60sta -j DROP &&
 ensure nat U60_RELAY_NAT -o u60sta -j MASQUERADE || return 1
 ipt -C INPUT -i u60sta -j U60_RELAY_IN || ipt -I INPUT 1 -i u60sta -j U60_RELAY_IN || return 1
 ipt -C FORWARD -j U60_RELAY_FWD || ipt -I FORWARD 1 -j U60_RELAY_FWD || return 1
 ipt -t nat -C POSTROUTING -j U60_RELAY_NAT || ipt -t nat -I POSTROUTING 1 -j U60_RELAY_NAT || return 1
 ip6tables -w 2 -C INPUT -i u60sta -j DROP 2>/dev/null || ip6tables -w 2 -I INPUT 1 -i u60sta -j DROP || return 1
}
withdraw() (
 exec 9>"$RUN/lease.lock";flock 9
 ip -4 route del default dev u60sta metric 50 2>/dev/null || :
 if [ -f "$RUN/ipv6-block" ];then ip -6 route del unreachable default metric 50 2>/dev/null || :;rm -f "$RUN/ipv6-block";fi
 rm -f "$RUN/lease-ready"
)
remove_rules() {
 while ipt -D INPUT -i u60sta -j U60_RELAY_IN;do :;done
 while ipt -D FORWARD -j U60_RELAY_FWD;do :;done
 while ipt -t nat -D POSTROUTING -j U60_RELAY_NAT;do :;done
 for chain in U60_RELAY_IN U60_RELAY_FWD;do ipt -F "$chain" || :;ipt -X "$chain" || :;done
 ipt -t nat -F U60_RELAY_NAT || :;ipt -t nat -X U60_RELAY_NAT || :
 while ip6tables -w 2 -D INPUT -i u60sta -j DROP 2>/dev/null;do :;done
}
stop_dhcp() {
 rm -f "$RUN/dhcp-generation"
 p=$(cat "$RUN/dhcp.pid" 2>/dev/null || :)
 case "$p" in ''|*[!0-9]*) ;; *)
  if [ -r /proc/$p/cmdline ] && tr '\000' ' ' < /proc/$p/cmdline | grep -q 'udhcpc.*-i u60sta';then kill "$p" 2>/dev/null || :;fi;;
 esac
 rm -f "$RUN/dhcp.pid"
}
cleanup() { rm -f "$RUN/enabled";stop_dhcp;withdraw;wpa terminate >/dev/null || :;[ ! -e /sys/class/net/u60sta ] || iw dev u60sta del;remove_rules;rm -f "$RUN/enabled"; }
status() {
 enabled=false;[ ! -f "$PRIVATE/enabled" ] || enabled=true
 active=false
 if [ -f "$RUN/lease-ready" ] && ip -4 route get 1.1.1.1 2>/dev/null | grep -q 'dev u60sta';then active=true;fi
 state=$(cat "$RUN/phase" 2>/dev/null || echo OFF)
 case "$state" in OFF|CONNECTING|CONNECTED|CELLULAR|CONFLICT|ERROR|POLICY) ;; *) state=ERROR;; esac
 [ "$enabled" = true ] || state=OFF
 [ "$active" != true ] || state=CONNECTED
 [ "$active:$state" != false:CONNECTED ] || state=CELLULAR
 saved=false;if [ -s "$PRIVATE/wpa.conf" ] && grep -q '^network={' "$PRIVATE/wpa.conf";then saved=true;fi
 printf '{"ok":true,"enabled":%s,"active":%s,"saved":%s,"state":"%s","ipv6":"cellular_blocked_while_relay"}\n' "$enabled" "$active" "$saved" "$state"
}
case "${1:-}" in
 rules) firewall;;
 status) status;;
 prepare) prepare;;
 scan-stop) [ -f "$PRIVATE/enabled" ] || cleanup;;
 enable)
  printf '1\n' > "$PRIVATE/enabled";touch "$RUN/enabled";phase CONNECTING
  /etc/init.d/u60-wifi-relay start;;
 on)
  [ -s "$PRIVATE/wpa.conf" ] && grep -q '^network={' "$PRIVATE/wpa.conf" || exit 1
  allowed || exit 1
  printf '1\n' > "$PRIVATE/enabled";touch "$RUN/enabled";phase CONNECTING
  /etc/init.d/u60-wifi-relay start;;
 off)
  rm -f "$PRIVATE/enabled" "$RUN/enabled"
  /etc/init.d/u60-wifi-relay stop >/dev/null 2>&1 || :
  cleanup;phase OFF;;
 watch)
  exec 8>"$RUN/watch.lock";flock -n 8 || exit 0
  trap 'trap - INT TERM EXIT;cleanup;exit' INT TERM EXIT
  tick=0
  while [ -f "$PRIVATE/enabled" ];do
   if [ -f /tmp/u60-standby/asleep ];then sleep 2;continue;fi
   # Stock sleep or a conflicting official setting must release our radio too.
   # Keep only the saved intent so a later stock wake can reconnect normally.
   if ! allowed; then cleanup;phase POLICY;sleep 5;continue;fi
   if ! prepare;then cleanup;phase ERROR;sleep 5;continue;fi
   touch "$RUN/enabled"
   if wpa status | grep -q '^wpa_state=COMPLETED$';then
    if [ ! -s "$RUN/dhcp.pid" ] || ! kill -0 "$(cat "$RUN/dhcp.pid")" 2>/dev/null;then
     phase CONNECTING
     U60_RELAY_IFINDEX=$(cat /sys/class/net/u60sta/ifindex);export U60_RELAY_IFINDEX
     U60_RELAY_GENERATION="$U60_RELAY_IFINDEX-$(date +%s)-$$";export U60_RELAY_GENERATION
     printf '%s\n' "$U60_RELAY_GENERATION" > "$RUN/dhcp-generation"
     udhcpc -f -i u60sta -p "$RUN/dhcp.pid" -s "$ROOT/wifi-relay-dhcp.sh" -t 3 -T 3 -A 10 >/dev/null 2>&1 8>&- &
    fi
    if [ -f "$RUN/lease-ready" ];then
     tick=$((tick+1));if [ "$tick" -ge 5 ];then firewall || { withdraw;phase ERROR; };tick=0;fi
    fi
   else stop_dhcp;withdraw;phase CELLULAR;fi
   sleep 2
  done;;
 *) exit 2;;
esac
