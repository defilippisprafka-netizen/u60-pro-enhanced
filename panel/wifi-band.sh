#!/bin/sh
# B28 needs its primary AP topology intact. Band switches control hostapd BSSs;
# explicit ON can recover a missing stack. Background reconciliation never wakes it.
set -u
ROOT=${ROOT:-/data/u60-panel}
HOSTAPD=${HOSTAPD:-/usr/sbin/hostapd_cli}
CTRL=${CTRL:-/data/vendor/wifi/hostapd}
UBUS=${UBUS:-/bin/ubus}
UCI=${UCI:-/sbin/uci}
JSONFILTER=${JSONFILTER:-/usr/bin/jsonfilter}
LOCK=${BAND_LOCK:-/tmp/u60-wifi-band.action}
WATCH_LOCK=${BAND_WATCH_LOCK:-/tmp/u60-wifi-band.watch}
CONTROL_LOCK=${BAND_CONTROL_LOCK:-/tmp/u60-control.lock}
LCD_BL=${LCD_BL:-/sys/class/leds/led:lcd/brightness}
WAIT=${BAND_WAIT_SECONDS:-75}
POLL=${BAND_POLL_SECONDS:-0.3}
PGREP=${BAND_PGREP:-/usr/bin/pgrep}
AP_INIT=${BAND_AP_INIT:-/etc/init.d/hostapd-daemon.init}
NET_CLASS=${BAND_NET_CLASS:-/sys/class/net}
CONF_DIR=${BAND_CONF_DIR:-/data/vendor/wifi}
UPTIME_FILE=${BAND_UPTIME_FILE:-/proc/uptime}
START_FILE=${BAND_START_FILE:-/tmp/u60-wifi-startup-missing}
now_s() {
 [ -r "$UPTIME_FILE" ] || { date +%s;return; }
 up=0;read -r up rest < "$UPTIME_FILE" || true;echo "${up%%.*}"
}
get() { "$UCI" -q get "wireless.$1" 2>/dev/null; }
lcd_on() { value=$(cat "$LCD_BL" 2>/dev/null || true);case "$value" in ''|*[!0-9]*) echo 0;; *) [ "$value" -gt 0 ] && echo 1 || echo 0;; esac; }
json() { "$JSONFILTER" -e "@.$1" 2>/dev/null; }
desired() {
 p=$(cat "$ROOT/wifi-$1-policy" 2>/dev/null || true)
 case "$p" in on|off) echo "$p";; *) [ "$(get "main_$1.disabled")" = 1 ] && echo off || echo on;; esac
}
save() { (umask 077; printf '%s\n' "$2" > "$ROOT/wifi-$1-policy.next") && mv "$ROOT/wifi-$1-policy.next" "$ROOT/wifi-$1-policy"; }
iface() {
 name=$(get "main_$1.ifname")
 case "$name" in wlan[0-9]) echo "$name";; *) [ "$1" = 2g ] && echo wlan0 || echo wlan2;; esac
}
ap() {
 band=$1; shift
 "$HOSTAPD" -p "$CTRL" -i "$(iface "$band")" "$@" 2>/dev/null & ap_pid=$!
 (sleep 2; kill "$ap_pid" 2>/dev/null) 9>&- >/dev/null 2>&1 & timer_pid=$!
 wait "$ap_pid"; ap_result=$?
 kill "$timer_pid" 2>/dev/null || true
 wait "$timer_pid" 2>/dev/null || true
 return "$ap_result"
}
state() {
 s=$(ap "$1" status | sed -n 's/^state=//p' | head -1)
 case "$s" in ENABLED|DISABLED|ACS|COUNTRY_UPDATE|HT_SCAN|DFS|UNINITIALIZED) echo "$s";; *) echo MISSING;; esac
}
idle() {
 status=$("$UBUS" -t 2 call zwrt_wlan status 2>/dev/null) || return 1
 [ "$(printf '%s' "$status" | json app_status):$(printf '%s' "$status" | json driver_status):$(printf '%s' "$status" | json pending)" = idle:idle:0 ]
}
within() { [ "$(now_s)" -lt "$deadline" ]; }
wait_idle() { while ! idle; do within || return 1; sleep "$POLL"; done; }
fail() { printf '{"ok":false,"message":"%s"}\n' "$1"; exit 1; }
ok() { printf '{"ok":true,"message":"%s"}\n' "$1"; exit 0; }
set_power() {
 # Whitelist scalars only. Never read or rewrite SSIDs, passwords or routing.
 lbd=$(get zte_mbb.lbd);case "$lbd" in 0|1) :;; *) return 1;; esac
 extra='';[ "$1" != 1 ] || extra=',"main_2g":{"disabled":"0"},"main_5g":{"disabled":"0"},"wifi0":{"disabled":"0"},"wifi1":{"disabled":"0"}'
 body="{\"source_module\":\"web\",\"zte_mbb\":{\"wifi_onoff\":\"$1\",\"lbd\":\"$lbd\"}$extra}"
 result=$("$UBUS" -t 3 call zwrt_wlan set "$body" 2>/dev/null) || return 1
 code=$(printf '%s' "$result" | json error_code);case "$code" in ''|0) :;; *) return 1;; esac
 error=$(printf '%s' "$result" | json error);[ -z "$error" ] || return 1
 result_code=$(printf '%s' "$result" | json result);case "$result_code" in ''|0|true|success) :;; *) return 1;; esac
 [ "$(get zte_mbb.wifi_onoff)" = "$1" ]
}
wait_off() {
 while :; do
  if idle && [ "$(state 2g)" = MISSING ] && [ "$(state 5g)" = MISSING ]; then return 0; fi
  within || return 1;sleep "$POLL"
 done
}
ready() { case "$(state "$1")" in ENABLED|DISABLED) return 0;; *) return 1;; esac; }
factory_starting() {
 # The sleep adapter reports app/driver idle even while these scripts run.
 "$PGREP" -f '(^|/)(wifi (up|down|ztereconf|reconf)|zte_start_wlan_at_boot.sh)( |$)|/etc/init.d/hostapd-daemon.init (start|stop|restart)( |$)' >/dev/null 2>&1
}
starting() {
 case "$(state 2g):$(state 5g)" in *ACS*|*DFS*|*HT_SCAN*|*COUNTRY_UPDATE*|*UNINITIALIZED*) return 0;; esac
 factory_starting
}
signature() {
 # Hash locally, never persist or log wireless credentials. Only runtime counters
 # and measured channel/power are excluded; changed user settings forbid repair.
 config=$("$UCI" -q show wireless 2>/dev/null) || return 1
 [ -n "$config" ] || return 1
 printf '%s\n' "$config" | sed '/\.sta_num=/d;/\.current_channel=/d;/\.txpower=/d' | sha256sum | cut -d ' ' -f1
 for name in wlan0 wlan2; do sha256sum "$CONF_DIR/hostapd-$name.conf" 2>/dev/null | cut -d ' ' -f1; done
}
record_ready() {
 idle && ! factory_starting || return 1
 ready 2g && ready 5g || return 1
 [ -s "$CONF_DIR/hostapd-wlan0.conf" ] && [ -s "$CONF_DIR/hostapd-wlan2.conf" ] || return 1
 sig=$(signature)
 [ "$(printf '%s\n' "$sig" | grep -Ec '^[0-9a-f]{64}$')" = 3 ] || return 1
 [ "$sig" != "$(cat "$ROOT/wifi-last-good.sha256" 2>/dev/null)" ] || return 0
 (umask 077; printf '%s\n' "$sig" > "$ROOT/wifi-last-good.sha256.next") && mv "$ROOT/wifi-last-good.sha256.next" "$ROOT/wifi-last-good.sha256"
}
repair_stalled_bss() {
 # Narrow B28 recovery: QCMAP sometimes creates wlan2 but omits its hostapd.
 # Never reuse an old generated config after settings changed, never duplicate a
 # live daemon, and never interfere with the factory's own setup/teardown.
 [ "$(desired 5g)" = on ] && [ "$(get zte_mbb.wifi_onoff)" = 1 ] || return 1
 [ -x "$AP_INIT" ] || return 1
 [ "$(state 5g)" = MISSING ] && ready 2g || { rm -f "$START_FILE"; return 1; }
 name=$(iface 5g);[ "$name" = wlan2 ] && [ -e "$NET_CLASS/$name/ifindex" ] || return 1
 factory_starting && { rm -f "$START_FILE"; return 1; }
 "$PGREP" -f '^/usr/sbin/hostapd .*hostapd-wlan2\.pid' >/dev/null 2>&1 && return 1
 [ -s "$ROOT/wifi-last-good.sha256" ] && [ "$(signature)" = "$(cat "$ROOT/wifi-last-good.sha256")" ] || return 1
 now=$(now_s);index=$(cat "$NET_CLASS/$name/ifindex");seen_index='';since=0
 [ ! -f "$START_FILE" ] || read -r seen_index since < "$START_FILE"
 if [ "$seen_index" != "$index" ]; then printf '%s %s\n' "$index" "$now" > "$START_FILE";return 1;fi
 case "$since" in ''|*[!0-9]*) return 1;; esac
 [ "$((now-since))" -ge 6 ] || return 1
 # Bound retries for this interface generation, including failed init calls.
 printf '%s %s\n' "$index" "$((now+300))" > "$START_FILE"
 "$AP_INIT" start "$name" >/dev/null 2>&1
}
resume_stack() {
 result=$("$UBUS" -t 3 call zwrt_wlan_adapter wifi_set_notify '{"wifi_opt":"1"}' 2>/dev/null) || return 1
 code=$(printf '%s' "$result" | json error_code);case "$code" in ''|0) :;; *) return 1;; esac
 error=$(printf '%s' "$result" | json error);[ -z "$error" ] || return 1
 result_code=$(printf '%s' "$result" | json result);case "$result_code" in ''|0|true|success) :;; *) return 1;; esac
 fast_end=$(( $(now_s) + 22 ))
 while within; do
  if idle && ready 2g && ready 5g; then return 0; fi
  repair_stalled_bss || true
  # Keep waiting for genuine ACS/DFS, never restart an AP during CAC.
  if [ "$(now_s)" -ge "$fast_end" ] && ! starting; then return 1;fi
  sleep "$POLL"
 done
 return 1
}
ensure_stack() {
 wait_idle || return 1
 power=$(get zte_mbb.wifi_onoff);case "$power" in 0|1) :;; *) return 1;; esac
 if [ "$power" = 1 ] && ready 2g && ready 5g; then return 0; fi
 if [ "$power" = 1 ]; then
  while starting; do
   within || return 1;sleep "$POLL"
   if ready 2g && ready 5g && idle;then return 0;fi
  done
  if ready 2g && ready 5g && idle;then return 0;fi
  # Resume through the same adapter used by stock sleep before rewriting power.
  if resume_stack;then return 0;fi
  within || return 1
 fi
 # A reload of an unchanged config does nothing. A missing stack needs a real
 # OFF->ON edge through the factory service, while preserving both band policies.
 if [ "$power" = 1 ]; then set_power 0 && wait_off || return 1; fi
 set_power 1 || return 1
 while :; do
  if idle && ready 2g && ready 5g; then return 0; fi
  within || return 1;sleep "$POLL"
 done
}
apply_band() {
 b=$1; target=$2;want=ENABLED;command=enable
 [ "$target" != off ] || { want=DISABLED;command=disable; }
 current=$(state "$b")
 if [ "$current" = MISSING ] && [ "$target" = off ]; then return 0; fi
 [ "$current" != "$want" ] || return 0
 [ "$(ap "$b" "$command")" = OK ] || return 1
 while [ "$(state "$b")" != "$want" ]; do within || return 1;sleep "$POLL";done
}
apply_policies() { apply_band 2g "$(desired 2g)" && apply_band 5g "$(desired 5g)" || return 1;record_ready || true; }
case "${1:-}" in
 record-ready) record_ready ;;
 status)
  s2=$(state 2g);s5=$(state 5g);power=$(get zte_mbb.wifi_onoff);available=false;case "$power" in 0|1) available=true;; esac
  on=false;[ "$s2" != ENABLED ] || on=true
  on5=false;[ "$s5" != ENABLED ] || on5=true
  managed=false;[ "$(desired 2g)" != off ] || managed=true
  managed5=false;[ "$(desired 5g)" != off ] || managed5=true
  active=false;[ "$on:$on5" = false:false ] || active=true
  configured=false;[ "$power" != 1 ] || configured=true
  busy=true;idle && busy=false
  case "$s2:$s5" in *ACS*|*DFS*|*HT_SCAN*|*COUNTRY_UPDATE*|*UNINITIALIZED*) busy=true;; esac
  printf '{"ok":true,"enabled":%s,"enabled_5g":%s,"available":%s,"managed_off":%s,"managed_off_5g":%s,"active":%s,"power_configured":%s,"busy":%s,"state_2g":"%s","state_5g":"%s"}\n' "$on" "$on5" "$available" "$managed" "$managed5" "$active" "$configured" "$busy" "$s2" "$s5"
  ;;
 on|off|on-5g|off-5g|power-on|power-off|reconcile|wake-recover|startup-check)
  case "$1" in wake-recover|startup-check) exec 8>"$CONTROL_LOCK";/usr/bin/flock -n 8 || exit 75;; esac
  exec 9>"$LOCK";tries=0
  until /usr/bin/flock -n 9; do
   [ "$1" != reconcile ] || exit 0
   case "$1" in wake-recover|startup-check) exit 75;; esac
   tries=$((tries+1));[ "$tries" -lt 30 ] || fail '无线正在处理上一项操作，请稍后重试';sleep 0.1
  done
  exec "$0" "locked-$1"
  ;;
 locked-startup-check)
  # Startup-only repair may finish an already-powered radio after auto-blank.
  # It cannot power a sleeping radio on or outlive the first three boot minutes.
  [ "$(get zte_mbb.wifi_onoff)" = 1 ] && [ "$(desired 5g)" = on ] || exit 0
  if ready 2g && ready 5g;then record_ready || true;exit 0;fi
  [ "$(now_s)" -lt 180 ] || exit 0
  repair_stalled_bss || true
  exit 75
  ;;
 locked-wake-recover)
  # A real off->on screen edge requests recovery, not a perpetual radio watchdog.
  # Respect explicit master OFF and band preferences; let stock startup run first.
  [ "$(lcd_on)" = 1 ] && [ "$(get zte_mbb.wifi_onoff)" = 1 ] || ok '屏幕未亮或总开关已关闭，未恢复无线'
  idle && ! factory_starting || exit 75
  need=false
  for b in 2g 5g;do
   if [ "$(desired "$b")" = on ] && [ "$(state "$b")" = MISSING ];then need=true;fi
  done
  [ "$need" = true ] || ok '无需恢复，保留当前频段状态'
  [ "$(lcd_on)" = 1 ] || ok '屏幕已熄灭，取消无线恢复'
  deadline=$(( $(now_s) + WAIT ))
  ensure_stack && apply_policies || fail '亮屏后的热点恢复未确认，请在 Wi-Fi 页面重试'
  ok '亮屏后已恢复原本开启的热点'
  ;;
 locked-on|locked-off|locked-on-5g|locked-off-5g|locked-power-on|locked-power-off)
  deadline=$(( $(now_s) + WAIT ));action=${1#locked-}
  wait_idle || fail '无线重配尚未结束，未确认操作，请稍后重试'
  p2=$(desired 2g);p5=$(desired 5g)
  if [ "$action" = power-off ]; then
   save 2g "$p2" && save 5g "$p5" || fail '频段设置保存失败，未关闭无线'
   set_power 0 && wait_off || fail '关闭请求未完成，未确认热点已停止'
   ok 'Wi-Fi 已关闭，两个热点已停止'
  fi
  if [ "$action" = power-on ]; then
   [ "$p2:$p5" != off:off ] || { p2=on;p5=on; }
   save 2g "$p2" && save 5g "$p5" || fail '无法保存频段设置'
   ensure_stack && apply_policies || fail 'Wi-Fi 启动未通过实际热点检查，请稍后重试'
   ok 'Wi-Fi 已开启，实际热点已回读确认'
  fi
  b=2g;case "$action" in *-5g) b=5g;; esac
  target=on;case "$action" in off*) target=off;; esac
  before=$(desired "$b")
  # Freeze both policies before repairing legacy disabled topology.
  save 2g "$p2" && save 5g "$p5" && save "$b" "$target" || fail '无法保存频段设置，未修改无线'
  if [ "$target" = on ]; then
   if ! ensure_stack; then save "$b" "$before";fail '无线服务启动失败，未确认热点已开启';fi
  fi
  if [ "$target" = on ]; then apply_policies; else apply_band "$b" off; fi
  if [ "$?" != 0 ]; then save "$b" "$before";fail '频段切换未通过实际检查，请刷新核对';fi
  ok '频段开关已生效并回读确认'
  ;;
 locked-reconcile)
  # Stock sleep and manual master OFF win. Never enable or rebuild in background.
  idle && ! factory_starting || exit 0
  # A temporarily idle adapter is not a completed multi-BSS startup.
  starting && exit 0
  for b in 2g 5g; do
   if [ "$(desired "$b")" = off ] && [ "$(state "$b")" = ENABLED ]; then ap "$b" disable >/dev/null;fi
  done
  ;;
 watch) exec /usr/bin/flock -n "$WATCH_LOCK" "$0" loop ;;
 loop)
  echo $$ > "$ROOT/wifi-band-watch.pid"
  trap 'rm -f "$ROOT/wifi-band-watch.pid";exit' INT TERM EXIT
  last_lcd=0;wake_at=0;idle_ticks=0;startup_pending=1
  while :;do
   lit=$(lcd_on);now=0
   if [ "$lit" = 0 ] && [ -f /tmp/u60-standby/asleep ];then last_lcd=0;wake_at=0;sleep 2;continue;fi
   if [ "$lit" = 1 ];then now=$(now_s);fi
   if [ "$startup_pending" = 1 ];then
    "$0" startup-check >/dev/null 2>&1;result=$?
    [ "$result" = 75 ] || startup_pending=0
   fi
   if [ "$lit" = 1 ] && [ "$last_lcd" = 0 ];then wake_at=$now;[ "$wake_at" -gt 0 ] || wake_at=1;fi
   if [ "$lit" = 0 ];then wake_at=0;fi
   last_lcd=$lit
   if [ "$wake_at" -gt 0 ] && [ "$now" -ge "$wake_at" ];then
    "$0" wake-recover > "$ROOT/wifi-wake-result.json.next" 2>/dev/null;result=$?
    if [ "$result" = 75 ];then wake_at=$((now+2));else mv "$ROOT/wifi-wake-result.json.next" "$ROOT/wifi-wake-result.json";wake_at=0;fi
   fi
   # Keep detecting wake every two seconds, but avoid full radio queries while dark.
   # Manual switches use the synchronous paths above and never wait for this loop.
   if [ "$lit" = 1 ];then
    "$0" reconcile >/dev/null 2>&1;idle_ticks=0
   elif [ "$idle_ticks" = 0 ];then
    "$0" reconcile >/dev/null 2>&1;idle_ticks=14
   else
    idle_ticks=$((idle_ticks-1))
   fi
   sleep 2
  done
  ;;
 *) fail '无效的无线操作' ;;
esac
