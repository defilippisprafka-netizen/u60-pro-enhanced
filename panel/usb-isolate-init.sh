#!/bin/sh /etc/rc.common
# Independent of /data and procd, which are not ready at this boot stage.
START=09
STOP=90
start() {
 req=$(cat /data/u60-panel/usb-role 2>/dev/null)
 if [ "$req" = LAN ] && [ "$(uci -q get zwrt_router.network.opms_wan_mode)" = PPP ]; then return 0; fi
 # Fail closed for unknown early-boot state; the late coordinator releases LAN.
 ebtables -L U60_USB_GUARD >/dev/null 2>&1 || ebtables -N U60_USB_GUARD || return 1
 ebtables -L U60_USB_GUARD 2>/dev/null | grep -q -- '-j DROP' || ebtables -A U60_USB_GUARD -j DROP || return 1
 for spec in 'INPUT -i' 'OUTPUT -o' 'FORWARD -i' 'FORWARD -o'; do
  set -- $spec
  ebtables -L "$1" 2>/dev/null | grep -F -q -- "$2 eth0 -j U60_USB_GUARD" || ebtables -A "$1" "$2" eth0 -j U60_USB_GUARD || return 1
 done
}
