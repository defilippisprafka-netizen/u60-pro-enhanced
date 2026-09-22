#!/bin/sh /etc/rc.common
START=99
STOP=88
USE_PROCD=1
start_service() {
 [ -x /data/u60-panel/panel-standby ] && [ -x /data/u60-panel/standby-services.sh ] || return 1
 procd_open_instance
 procd_set_param command /data/u60-panel/panel-standby watch
 procd_set_param respawn 3600 5 5
 procd_close_instance
}
