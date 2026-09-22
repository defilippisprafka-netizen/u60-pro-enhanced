#!/bin/sh
# Run deliberately to create this device's own Tailscale identity and login.
set -eu
umask 077
[ "$(id -u)" = 0 ]
[ -c /dev/net/tun ]
[ ! -e /data/tailscale/tailscaled.state ] || { echo 'Identity already exists; use the regular service controls.';exit 1; }
exec 9>/tmp/u60-tailscale-mode.lock
flock -n 9 || { echo 'Another Tailscale operation is running';exit 1; }
[ ! -S /tmp/tailscale/tailscaled.sock ] || { echo 'Daemon already running';exit 1; }
mkdir -p /tmp/tailscale
chmod 700 /tmp/tailscale
export GOGC=25 GOMEMLIMIT=192MiB
/data/tailscale/bin/tailscaled --tun=tailscale0 --state=/data/tailscale/tailscaled.state --statedir=/tmp/tailscale --socket=/tmp/tailscale/tailscaled.sock --port=41641 --no-logs-no-support </dev/null >/dev/null 2>&1 9>&- &
printf '%s\n' "$!" > /data/tailscale/tailscaled.pid
printf 'tun\n' > /data/u60-panel/tailscale-mode
n=0
while [ ! -S /tmp/tailscale/tailscaled.sock ];do n=$((n+1));[ "$n" -lt 30 ] || exit 1;sleep 1;done
flock -u 9
/data/tailscale/bin/tailscale --socket=/tmp/tailscale/tailscaled.sock up --accept-dns=false --hostname=u60-pro
