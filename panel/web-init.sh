#!/bin/sh /etc/rc.common
START=99
STOP=89
USE_PROCD=1
start_service() {
 [ -x /data/u60-panel/panel-web ] || return 1
 /data/u60-web/mount.sh start || return 1
 procd_open_instance
 procd_set_param command /data/u60-panel/panel-web
 procd_set_param respawn 3600 5 5
 procd_set_param stdout 0
 procd_set_param stderr 0
 procd_close_instance
}
stop_service() {
 /data/u60-web/mount.sh stop
}
