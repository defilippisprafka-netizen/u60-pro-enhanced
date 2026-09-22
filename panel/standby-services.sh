#!/bin/sh
# Transition-only helper. Never changes WantRunning, proxy choice, saved profile,
# credentials, Wi-Fi policy or the stock power manager's wakelocks.
set -u
umask 077
BASE=${U60_STANDBY_TEST_ROOT:-}
case "$BASE" in ''|/*) ;; *) exit 2;; esac
ROOT="$BASE/data/u60-panel"
RUN="$BASE/tmp/u60-standby"
PROC="$BASE/proc"
action=${1:-}
case "$action" in pause|resume) ;; *) exit 2;; esac
[ ! -L "$RUN" ] || exit 1
mkdir -p "$RUN" || exit 1
chmod 700 "$RUN" || exit 1
exec 9>"$RUN/action.lock"
flock -n 9 || exit 75
phase() { printf '%s\n' "$1" > "$RUN/phase.next" && mv "$RUN/phase.next" "$RUN/phase"; }
event() {
 stamp=0;read -r stamp unused < "$PROC/uptime" || :
 capacity=unknown;read -r capacity < "$BASE/sys/class/power_supply/battery/capacity" || :
 printf '%s phase=%s battery=%s\n' "$stamp" "$1" "$capacity" >> "$RUN/events.log"
 tail -n 32 "$RUN/events.log" > "$RUN/events.next" && mv "$RUN/events.next" "$RUN/events.log"
}
fail() { phase error;event error;exit 1; }
owned() {
 which=$1;target=$2
 case "$target" in ''|*[!0-9]*|0|1) return 1;; esac
 if [ "$which" = clash ];then
  [ "$(readlink "$PROC/$target/exe" 2>/dev/null)" = "$BASE/data/u60-clash/mihomo" ] || return 1
  tr '\000' '\n' < "$PROC/$target/cmdline" | grep -Fx -- "$BASE/data/u60-clash/config.yaml" >/dev/null 2>&1
 else
  [ "$(readlink "$PROC/$target/exe" 2>/dev/null)" = "$BASE/data/tailscale/bin/tailscaled" ] || return 1
  tr '\000' '\n' < "$PROC/$target/cmdline" | grep -Fx -- "--state=$BASE/data/tailscale/tailscaled.state" >/dev/null 2>&1 &&
  tr '\000' '\n' < "$PROC/$target/cmdline" | grep -Fx -- "--socket=$BASE/tmp/tailscale/tailscaled.sock" >/dev/null 2>&1
 fi
}
find_owner() {
 found=
 for f in "$PROC"/[0-9]*;do
  p=${f##*/};owned "$1" "$p" || continue
  [ -z "$found" ] || return 1
  found=$p
 done
}
stop_owned() {
 service=$1;target=$2
 [ -n "$target" ] || return 0
 owned "$service" "$target" || return 0
 if [ -n "$BASE" ];then "$BASE/bin/kill" -TERM "$target" || return 1;else kill -TERM "$target" || return 1;fi
 count=0
 while owned "$service" "$target";do
  count=$((count+1));[ "$count" -lt 40 ] || return 1;sleep .2
 done
}
save_resume() { printf '%s %s %s\n' "$clash" "$ts" "$profile" > "$RUN/resume.next" && mv "$RUN/resume.next" "$RUN/resume"; }
resume() {
 [ -f "$RUN/active" ] || return 0
 clash=;ts=;profile=;extra=
 read -r clash ts profile extra < "$RUN/resume" || fail
 case "$clash:$ts" in 0:0|0:1|1:0|1:1) ;; *) fail;; esac
 case "$profile" in clash|direct|tailscale) ;; *) fail;; esac
 [ -z "$extra" ] || fail
 # Let the normal cellular/Wi-Fi recovery proceed concurrently. Keep the active
 # guard until original services are restored so background jobs cannot start
 # duplicates or change the saved Internet profile in the middle of this work.
 phase waking || fail
 rm -f "$RUN/asleep"
 if [ "$ts" = 1 ];then
  "$ROOT/tailscale-mode.sh" standby-resume >/dev/null 2>&1 8>&- 9>&- || fail
  ts=0;save_resume || fail
 fi
 if [ "$clash" = 1 ];then
  "$ROOT/network-profile.sh" standby-resume >/dev/null 2>&1 8>&- 9>&- || fail
  clash=0;save_resume || fail
 fi
 "$ROOT/tailscale-lan.sh" reconcile >/dev/null 2>&1 8>&- 9>&- || fail
 phase awake || fail
 rm -f "$RUN/active" "$RUN/asleep" "$RUN/resume"
 event awake
}
if [ "$action" = resume ];then resume;exit $?;fi
[ ! -f "$RUN/active" ] || exit 0
exec 8>"$BASE/tmp/u60-control.lock"
flock -n 8 || exit 75
"$ROOT/panel-standby" eligible || exit 75
find_owner clash || exit 1;clash_pid=$found
find_owner tailscale || exit 1;ts_pid=$found
profile=;read -r profile < "$ROOT/network-profile" || exit 1
case "$profile" in clash|direct|tailscale) ;; *) exit 1;; esac
clash=0;ts=0;[ -z "$clash_pid" ] || clash=1;[ -z "$ts_pid" ] || ts=1
save_resume && phase entering && touch "$RUN/active" "$RUN/asleep" || fail
# The control endpoint now rejects user mutations while active. Release its
# lock before waiting on daemons; the original radio wake must not be blocked.
flock -u 8;exec 8>&-
"$ROOT/panel-standby" eligible || { resume;exit $?; }
stop_owned clash "$clash_pid" || fail
"$ROOT/panel-standby" eligible || { resume;exit $?; }
stop_owned tailscale "$ts_pid" || fail
"$ROOT/panel-standby" eligible || { resume;exit $?; }
phase asleep || fail
event asleep
exit 0
