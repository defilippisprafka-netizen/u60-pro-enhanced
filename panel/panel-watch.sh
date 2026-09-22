#!/bin/sh
# u60-panel maps SINGLE=SIGUSR1, LONG=SIGHUP, DOUBLE=SIGTERM.
# No KEY_POWER event is ever injected.
set -u
ROOT="${ROOT:-/data/u60-panel}"
BIN="$ROOT/u60-panel"
LOG="$ROOT/watch.log"
WATCH_LOCK="/tmp/u60-panel-watch"
CHILD=""
log() { echo "$(date '+%H:%M:%S') $*" >> "$LOG"; }
cleanup() {
	trap - EXIT INT TERM HUP
	[ -n "$CHILD" ] && kill "$CHILD" 2>/dev/null || true
	[ -n "$CHILD" ] && wait "$CHILD" 2>/dev/null || true
	rm -f "$WATCH_LOCK/owner"; rmdir "$WATCH_LOCK" 2>/dev/null || true
}
[ -x "$BIN" ] || { log "binary missing, watcher not started"; exit 1; }
if ! mkdir "$WATCH_LOCK" 2>/dev/null; then
	owner=$(cat "$WATCH_LOCK/owner" 2>/dev/null || true)
	[ -n "$owner" ] && kill -0 "$owner" 2>/dev/null && exit 0
	rm -f "$WATCH_LOCK/owner"; rmdir "$WATCH_LOCK" 2>/dev/null || true
	mkdir "$WATCH_LOCK" 2>/dev/null || exit 0
fi
echo $$ > "$WATCH_LOCK/owner"
trap cleanup EXIT INT TERM HUP
log "watch start"
"$BIN" watch &
CHILD=$!
wait "$CHILD"
status=$?
CHILD=""
log "watch exit status=$status"
exit "$status"
