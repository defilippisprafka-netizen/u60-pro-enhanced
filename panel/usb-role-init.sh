#!/bin/sh /etc/rc.common
# Install after /data and ubus are available; early isolation has its own service.
START=99
STOP=89
USE_PROCD=1
start_service() {
 [ -x /data/u60-panel/usb-role.sh ] || return 1
 /data/u60-panel/usb-role.sh prepare || return 1
 procd_open_instance
 procd_set_param command /data/u60-panel/usb-role.sh boot-watch
 procd_set_param respawn 3600 5 5
 procd_close_instance
}
