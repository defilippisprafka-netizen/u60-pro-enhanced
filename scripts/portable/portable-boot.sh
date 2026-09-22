#!/bin/sh
# This machine starts its own configured services; no identity or node is cloned.
umask 077
(
 sleep 20
 [ ! -s /data/tailscale/tailscaled.state ] || sh /data/tailscale/tailscale-start.sh
 [ ! -s /data/u60-clash/config.yaml ] || sh /data/u60-clash/start.sh
) </dev/null >/dev/null 2>&1 &
exec /data/u60-panel/panel-autostart.sh
