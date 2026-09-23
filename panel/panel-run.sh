#!/bin/sh
# Own the LCD for u60-panel, then restore a visible factory UI on every exit.
set -u
ROOT="${ROOT:-/data/u60-panel}"
BIN="$ROOT/u60-panel"
LOG="$ROOT/panel.log"
LOCK="${PANEL_LOCK:-/tmp/u60-panel.lock}"
RUN_LOCK="${PANEL_RUN_LOCK:-/tmp/u60-panel.run}"
HB="${PANEL_HB:-/tmp/u60-panel.hb}"
WD_MARK="${PANEL_WD_MARK:-/tmp/u60-panel.watchdog}"
LCD_BL="${LCD_BL:-/sys/class/leds/led:lcd/brightness}"
FACTORY_INIT="${FACTORY_INIT:-/etc/init.d/zte_topsw_devui}"
PROC_MOUNTS="${PROC_MOUNTS:-/proc/mounts}"
UPTIME_FILE="${UPTIME_FILE:-/proc/uptime}"
ANIM_DIR="${ANIM_DIR:-/usr/ui/anim}"
NOANIM_DIR="${NOANIM_DIR:-/tmp/u60-noanim}"
BOOT_DONE="${BOOT_DONE:-/tmp/zte_boot_done}"
REBOOT_FILE="${REBOOT_FILE:-/tmp/touchui_reboot_file}"
LCD_SAVED=""
MTDEV_ORIGINAL=0
CHILD=""
WD=""
OWNER="$$"

# An updater may hold the settings flock while launching us. A long-lived UI
# must not inherit that open file description into itself or its watchdog.
CONTROL_LOCK="${PANEL_CONTROL_LOCK:-/tmp/u60-control.lock}"
for inherited in "${PANEL_FD_DIR:-/proc/$$/fd}"/*; do
	fd=${inherited##*/}
	case "$fd" in ''|*[!0-9]*|0|1|2) continue ;; esac
	[ "$(readlink "$inherited" 2>/dev/null || true)" = "$CONTROL_LOCK" ] || continue
	eval "exec $fd>&-"
done

log() { echo "$(date '+%H:%M:%S') $*" >> "$LOG"; }
uptime_s() { awk '{ print int($1) }' "$UPTIME_FILE" 2>/dev/null || echo 0; }
switch_log() {
	read -r elapsed rest < "$UPTIME_FILE" 2>/dev/null || elapsed=unknown
	log "switch mono_s=$elapsed $*"
}
set_bl() { [ -w "$LCD_BL" ] && echo "$1" > "$LCD_BL" 2>/dev/null || true; }

cleanup_lock() {
	[ -f "$RUN_LOCK/owner" ] && [ "$(cat "$RUN_LOCK/owner" 2>/dev/null)" = "$OWNER" ] || return 0
	rm -f "$LOCK" "$HB" "$WD_MARK" "$RUN_LOCK/owner"
	rmdir "$RUN_LOCK" 2>/dev/null || true
}
unhide_boot_anim() {
	if grep -F -q " $ANIM_DIR " "$PROC_MOUNTS" 2>/dev/null; then
		umount "$ANIM_DIR" 2>/dev/null || umount -l "$ANIM_DIR" 2>/dev/null || true
	fi
}
hide_boot_anim() {
	mkdir -p "$NOANIM_DIR"
	if ! grep -F -q " $ANIM_DIR " "$PROC_MOUNTS" 2>/dev/null; then
		mount --bind "$NOANIM_DIR" "$ANIM_DIR" 2>/dev/null || true
	fi
	echo 1 > "$BOOT_DONE"
	echo 0 > "$REBOOT_FILE"
}
factory_alive() { pidof zte_topsw_devui mtdev2tuio >/dev/null 2>&1; }
wait_factory_gone() {
	n=0
	while factory_alive && [ "$n" -lt 40 ]; do
		sleep 0.05; n=$((n + 1))
	done
	! factory_alive
}
stop_factory() {
	switch_log "event=stop-factory"
	unhide_boot_anim
	"$FACTORY_INIT" stop >/dev/null 2>&1 || true
	# Both owners can release their devices concurrently; never start the
	# panel until the stock renderer AND its touch bridge have gone.
	for name in zte_topsw_devui mtdev2tuio; do
		pids=$(pidof "$name" 2>/dev/null || true)
		[ -n "$pids" ] && kill $pids 2>/dev/null || true
	done
	if ! wait_factory_gone; then
		switch_log "event=force-stop-factory"
		for name in zte_topsw_devui mtdev2tuio; do
			pids=$(pidof "$name" 2>/dev/null || true)
			[ -n "$pids" ] && kill -9 $pids 2>/dev/null || true
		done
		wait_factory_gone || { log "factory still owns display/touch; panel start refused"; return 1; }
	fi
	switch_log "event=factory-released"
}
restore_factory() {
	switch_log "event=restore-factory"
	hide_boot_anim
	restore_bl="${LCD_SAVED:-80}"
	case "$restore_bl" in ''|*[!0-9]*|0) restore_bl=80 ;; esac
	set_bl "$restore_bl"
	"$FACTORY_INIT" start >/dev/null 2>&1 || true
	n=0
	while [ "$n" -lt 50 ]; do
		pidof zte_topsw_devui >/dev/null 2>&1 && break
		sleep 0.05; n=$((n + 1))
	done
	if pidof zte_topsw_devui >/dev/null 2>&1; then
		switch_log "event=factory-process-visible"
	else
		log "factory process not visible before restore timeout"
	fi
	if [ "$MTDEV_ORIGINAL" = 1 ] && ! pidof mtdev2tuio >/dev/null 2>&1 && [ -x /usr/bin/mtdev2tuio ]; then
		mtdev2tuio /dev/input/event3 osc.udp://127.0.0.1:3333/ >/dev/null 2>&1 &
	fi
}
stop_children() {
	[ -n "$WD" ] && kill "$WD" 2>/dev/null || true
	if [ -n "$CHILD" ] && kill -0 "$CHILD" 2>/dev/null; then
		kill "$CHILD" 2>/dev/null || true
		n=0
		while kill -0 "$CHILD" 2>/dev/null && [ "$n" -lt 40 ]; do
			sleep 0.05; n=$((n + 1))
		done
		kill -9 "$CHILD" 2>/dev/null || true
	fi
	[ -n "$CHILD" ] && wait "$CHILD" 2>/dev/null || true
	[ -n "$WD" ] && wait "$WD" 2>/dev/null || true
}
stop_watchdog() {
	[ -n "$WD" ] && kill "$WD" 2>/dev/null || true
	[ -n "$WD" ] && wait "$WD" 2>/dev/null || true
	WD=""
}
start_watchdog() {
	(
		# The panel heartbeat uses CLOCK_MONOTONIC (suspend excluded), while
		# /proc/uptime includes suspend. Comparing their absolute values
		# falsely kills a healthy panel after deep sleep. Observe progress
		# instead: nine unchanged checks allow ~27 awake seconds to recover.
		# Suspended time does not accumulate checks, but a truly stuck UI
		# still triggers the existing retry/factory fallback.
		last_hb=""
		stale_checks=0
		sleep 4
		while [ -f "$LOCK" ]; do
			hb=$(cat "$HB" 2>/dev/null || true)
			case "$hb" in
				''|*[!0-9]*) stale_checks=$((stale_checks + 1)) ;;
				*) if [ "$hb" = "$last_hb" ]; then
					stale_checks=$((stale_checks + 1))
				else
					last_hb="$hb"; stale_checks=0
				fi ;;
			esac
			if [ "$stale_checks" -ge 9 ]; then
				log "watchdog: heartbeat not advancing checks=$stale_checks"
				: > "$WD_MARK"
				pid=$(cat "$LOCK" 2>/dev/null || true)
				[ -n "$pid" ] && kill "$pid" 2>/dev/null || true
				break
			fi
			sleep 3
		done
	) >/dev/null 2>&1 &
	WD=$!
}
finish() {
	trap - EXIT INT TERM HUP
	stop_children
	restore_factory
	cleanup_lock
}

[ -x "$BIN" ] || { echo "u60-panel missing" >&2; exit 1; }
if ! mkdir "$RUN_LOCK" 2>/dev/null; then
	owner=$(cat "$RUN_LOCK/owner" 2>/dev/null || true)
	if [ -n "$owner" ] && kill -0 "$owner" 2>/dev/null; then
		log "already running wrapper=$owner"; exit 0
	fi
	rm -f "$RUN_LOCK/owner"; rmdir "$RUN_LOCK" 2>/dev/null || true
	mkdir "$RUN_LOCK" 2>/dev/null || { log "run lock busy"; exit 0; }
fi
echo "$OWNER" > "$RUN_LOCK/owner"
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

[ -r "$LCD_BL" ] && LCD_SAVED=$(cat "$LCD_BL" 2>/dev/null || echo 0)
if pidof mtdev2tuio >/dev/null 2>&1; then MTDEV_ORIGINAL=1; fi
stop_factory || exit 1
set_bl 255
uptime_s > "$HB"
attempt=0
status=0
while :; do
	rm -f "$WD_MARK"
	switch_log "event=start-panel attempt=$attempt"
	"$BIN" &
	CHILD=$!
	echo "$CHILD" > "$LOCK"
	start_watchdog
	if wait "$CHILD"; then status=0; else status=$?; fi
	CHILD=""
	stop_watchdog
	log "panel exit status=$status watchdog=$([ -f "$WD_MARK" ] && echo 1 || echo 0)"
	[ "$status" -eq 0 ] && [ ! -f "$WD_MARK" ] && break
	if [ "$attempt" -ge 1 ]; then break; fi
	attempt=$((attempt + 1))
	uptime_s > "$HB"
	log "restart panel once after unexpected exit"
done
exit "$status"
