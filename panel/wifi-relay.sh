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
ap_control() {
 (
  interface=$1;shift
  hostapd_cli -p /data/vendor/wifi/hostapd -i "$interface" "$@" 2>/dev/null & p=$!
  (sleep 3;kill "$p" 2>/dev/null) 7>&- 8>&- 9>&- >/dev/null 2>&1 & timer=$!
  wait "$p";r=$?;kill "$timer" 2>/dev/null || :;wait "$timer" 2>/dev/null || :;exit "$r"
 )
}
radio_args() {
 # freq, width code (0=20/40, 1=80, 2=160), secondary offset, center index.
 case "$1:$2:$3:$4" in *[!0-9:-]*) return 1;; esac
 frequency=$1;width=$2;offset=$3;center=$4
 case "$width" in
  0) bw=20;cf=$frequency;[ "$offset" = 0 ] || { bw=40;cf=$((frequency+offset*10)); };;
  1) bw=80;cf=$((5000+center*5));;
  2) bw=160;cf=$((5000+center*5));;
  *) return 1;;
 esac
 [ "$offset" = -1 ] || [ "$offset" = 0 ] || [ "$offset" = 1 ] || return 1
 [ "$frequency" -ge 2400 ] && [ "$frequency" -le 5900 ] && [ "$cf" -ge 2400 ] && [ "$cf" -le 5900 ] || return 1
 extra='ht he eht';[ "$frequency" -lt 3000 ] || extra='ht vht he eht'
 printf '%s\n' "5 $frequency bandwidth=$bw center_freq1=$cf sec_channel_offset=$offset $extra"
}
radio_geometry() {
 printf '%s\n' "$1" | awk -F= '
 $1=="freq" {f=$2} $1=="vht_oper_chwidth" {w=$2} $1=="secondary_channel" {o=$2} $1=="vht_oper_centr_freq_seg0_idx" {c=$2}
 END {if(f)printf "%s %s %s %s\n",f,w?w:0,o?o:0,c?c:0}'
}
radio_driver_frequency() {
 iw dev "$1" info 2>/dev/null | sed -n 's/.*(\([0-9][0-9]*\) MHz).*/\1/p' | head -1
}
radio_configure() (
 interface=$1;shift
 radio_args "$@" >/dev/null || exit 1
 frequency=$1;width=$2;offset=$3;center=$4
 channel=$(((frequency-5000)/5));[ "$frequency" -ge 3000 ] || channel=$(((frequency-2407)/5))
 ht='[SHORT-GI-20]'
 case "$offset" in 1) ht='[HT40+][SHORT-GI-20][SHORT-GI-40]';; -1) ht='[HT40-][SHORT-GI-20][SHORT-GI-40]';; esac
 ac=1;[ "$frequency" -ge 3000 ] || ac=0
 for pair in "channel $channel" 'ieee80211n 1' "ieee80211ac $ac" "ht_capab $ht" "vht_oper_chwidth $width" "vht_oper_centr_freq_seg0_idx $center" "he_oper_chwidth $width" "he_oper_centr_freq_seg0_idx $center" "eht_oper_chwidth $width" "eht_oper_centr_freq_seg0_idx $center";do
  key=${pair%% *};value=${pair#* }
  [ "$(ap_control "$interface" set "$key" "$value")" = OK ] || exit 1
 done
)
radio_apply() (
 interface=$1;shift
 radio_args "$@" >/dev/null || exit 1
 before=$(ap_control "$interface" status);original=$(radio_geometry "$before")
 [ -n "$original" ] || exit 1
 # B28 reports CSA completion before firmware completes it; subsequent key
 # installation fails. Reconfigure a stopped BSS instead, without rewriting UCI.
 [ "$(ap_control "$interface" disable)" = OK ] || exit 1
 if ! radio_configure "$interface" "$@";then
  radio_configure "$interface" $original || :
  ap_control "$interface" enable >/dev/null
  exit 1
 fi
 if [ "$(ap_control "$interface" enable)" != OK ];then
  radio_configure "$interface" $original || :
  ap_control "$interface" enable >/dev/null
  exit 1
 fi
 tries=0
 while [ "$tries" -lt 12 ];do
  actual=$(ap_control "$interface" status)
  state=$(printf '%s\n' "$actual" | sed -n 's/^state=//p')
  freq=$(printf '%s\n' "$actual" | sed -n 's/^freq=//p')
  driver=$(radio_driver_frequency "$interface")
  [ "$state:$freq:$driver" != "ENABLED:$1:$1" ] || exit 0
  tries=$((tries+1));sleep .25
 done
 exit 1
)
radio_lock() {
 attempts=0
 while ! flock -n 7;do attempts=$((attempts+1));[ "$attempts" -lt 20 ] || return 1;sleep .2;done
}
radio_align() (
 f=${1:-};case "$f" in ''|*[!0-9]*) exit 1;; esac
 if [ "$f" -ge 2412 ] && [ "$f" -le 2472 ] && [ "$(((f-2412)%5))" = 0 ];then
  interface=wlan0;geometry="$f 0 0 0"
 else
  case "$f" in
   5180|5200|5220|5240) center=42;;
   5745|5765|5785|5805) center=155;;
   5825) center=165;;
   *) exit 1;;
  esac
  interface=wlan2
  if [ "$f" = 5825 ];then geometry="$f 0 0 165";else
   offset=1;case "$f" in 5200|5240|5765|5805) offset=-1;; esac
   geometry="$f 1 $offset $center"
  fi
 fi
 exec 7>/tmp/u60-wifi-band.action;radio_lock || exit 1
 before=$(ap_control "$interface" status)
 [ "$(printf '%s\n' "$before" | sed -n 's/^state=//p')" = ENABLED ] || exit 0
 old=$(radio_geometry "$before");[ -n "$old" ] || exit 1
 current=${old%% *};driver=$(radio_driver_frequency "$interface")
 [ "$current:$driver" != "$f:$f" ] || exit 0
 # Keep the first geometry so each retry doesn't overwrite the original.
 snapshot="$RUN/radio-$interface"
 if [ ! -f "$snapshot" ];then radio_args $old >/dev/null || exit 1;printf '%s\n' "$old" > "$snapshot";fi
 radio_apply "$interface" $geometry
)
radio_restore() (
 exec 7>/tmp/u60-wifi-band.action;radio_lock || exit 1
 result=0
 for interface in wlan0 wlan2;do
  snapshot="$RUN/radio-$interface";[ -f "$snapshot" ] || continue
  old=$(cat "$snapshot");case "$old" in ''|*[!0-9\ -]*) result=1;continue;; esac
  before=$(ap_control "$interface" status)
  if [ "$(printf '%s\n' "$before" | sed -n 's/^state=//p')" = ENABLED ];then
   current=$(printf '%s\n' "$before" | sed -n 's/^freq=//p')
   driver=$(radio_driver_frequency "$interface")
   if [ "$current:$driver" != "${old%% *}:${old%% *}" ];then radio_apply "$interface" $old || { result=1;continue; };fi
  fi
  rm -f "$snapshot"
 done
 exit "$result"
)
allowed() {
 [ "$(cat "$ROOT/usb-role" 2>/dev/null)" = LAN ] &&
 [ "$(setting zwrt_router.network.opms_wan_mode)" = PPP ] &&
 [ "$(setting wireless.zte_mbb.wifi_onoff)" = 1 ] &&
 [ "$(ap_state wlan2)" = ENABLED ] &&
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
  wpa disconnect >/dev/null
  [ "$(wpa driver SETROAMMODE 1)" = OK ] || return 1
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
cleanup() { rm -f "$RUN/enabled";stop_dhcp;withdraw;wpa driver SETROAMMODE 0 >/dev/null || :;wpa terminate >/dev/null || :;[ ! -e /sys/class/net/u60sta ] || iw dev u60sta del;remove_rules;radio_restore || :;rm -f "$RUN/enabled"; }
status() {
 enabled=false;[ ! -f "$PRIVATE/enabled" ] || enabled=true
 active=false
 if [ -f "$RUN/lease-ready" ] && ip -4 route get 1.1.1.1 2>/dev/null | grep -q 'dev u60sta';then active=true;fi
 state=$(cat "$RUN/phase" 2>/dev/null || echo OFF)
 case "$state" in OFF|CONNECTING|CONNECTED|CELLULAR|CONFLICT|ERROR|POLICY) ;; *) state=ERROR;; esac
 [ "$enabled" = true ] || state=OFF
 [ "$active" != true ] || state=CONNECTED
 [ "$active:$state" != false:CONNECTED ] || state=CELLULAR
 frequency=$(wpa status | sed -n "s/^freq=//p");case "$frequency" in ''|*[!0-9]*) frequency=0;; esac
 saved=false;if [ -s "$PRIVATE/wpa.conf" ] && grep -q '^network={' "$PRIVATE/wpa.conf";then saved=true;fi
 printf '{"ok":true,"enabled":%s,"active":%s,"saved":%s,"state":"%s","frequency":%s,"ipv6":"cellular_blocked_while_relay"}\n' "$enabled" "$active" "$saved" "$state" "$frequency"
}
stop_watch() {
 /etc/init.d/u60-wifi-relay stop >/dev/null 2>&1 || :
 # procd stop returns before the previous shell finishes its cleanup.
 # Wait for its actual ownership lock before touching radio/route resources.
 exec 8>"$RUN/watch.lock"
 n=0
 while ! flock -n 8;do n=$((n+1));[ "$n" -lt 125 ] || return 1;sleep .2;done
}
case "${1:-}" in
 align) radio_align "${2:-}";;
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
  stop_watch || exit 1
  cleanup;phase OFF;;
 watch)
  exec 8>"$RUN/watch.lock";flock -n 8 || exit 0
  coordinate_pid=''
  finish() { trap - INT TERM HUP EXIT;[ -z "$coordinate_pid" ] || { kill "$coordinate_pid" 2>/dev/null || :;wait "$coordinate_pid" 2>/dev/null || :; };cleanup;exit; }
  trap finish INT TERM HUP EXIT
  tick=0
  while [ -f "$PRIVATE/enabled" ];do
   if [ -f /tmp/u60-standby/asleep ];then sleep 2;continue;fi
   # Stock sleep or a conflicting official setting must release our radio too.
   # Keep only the saved intent so a later stock wake can reconnect normally.
   if ! allowed; then cleanup;phase POLICY;sleep 5;continue;fi
   if ! prepare;then cleanup;phase ERROR;sleep 5;continue;fi
   touch "$RUN/enabled"
   station=$(wpa status)
   if printf '%s\n' "$station" | grep -q '^wpa_state=COMPLETED$';then
    frequency=$(printf '%s\n' "$station" | sed -n 's/^freq=//p')
    if ! radio_align "$frequency";then stop_dhcp;withdraw;wpa disconnect >/dev/null;phase ERROR;sleep 5;continue;fi
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
   else
    stop_dhcp;withdraw;phase CELLULAR
    "$ROOT/panel-relay" coordinate >/dev/null 2>&1 8>&- & coordinate_pid=$!
    if wait "$coordinate_pid";then coordinate_pid='';sleep 2
    else coordinate_pid='';cleanup;phase CELLULAR;sleep 10;fi
   fi
   sleep 2
  done;;
 *) exit 2;;
esac
