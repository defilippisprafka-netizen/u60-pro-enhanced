#!/bin/sh
# A single observer for the usage ledger and an explicitly verified charge policy.
# It never manages the screen, Wi-Fi, routing, or Clash. No request output is logged.
set -u
ROOT="${ROOT:-/data/u60-panel}"
LOCK="${MAINTENANCE_LOCK:-/tmp/u60-panel-maintenance.lock}"
PIDFILE="${MAINTENANCE_PIDFILE:-/tmp/u60-panel-maintenance.pid}"
case "${1:-}" in
 watch)
  umask 077
  exec 9>"$LOCK" || exit 1
  flock -n 9 || exit 0
  echo "$$" > "$PIDFILE"
  trap 'rm -f "$PIDFILE"' EXIT
  trap 'exit 0' INT TERM HUP
  while :; do
   if [ ! -f /tmp/u60-standby/asleep ];then
    ( exec 9>&-; printf '%s\n' '{"action":"maintenance.tick","args":{}}' | "$ROOT/panel-control" >/dev/null 2>&1 )
   fi
   sleep 30 9>&-
  done
  ;;
 *) exit 2 ;;
esac
