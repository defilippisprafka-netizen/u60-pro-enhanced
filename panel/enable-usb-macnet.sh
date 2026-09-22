#!/bin/sh
# Switch USB gadget from RNDIS to CDC ECM so macOS C2C gets an Ethernet NIC.
# Keeps ADB (ffs). Does not change Wi-Fi.
set -u
LOG=/data/u60-panel/usb-macnet.log
log() { echo "$(date '+%H:%M:%S') $*" >> "$LOG"; }

sleep 2
f1=$(readlink /sys/kernel/config/usb_gadget/g1/configs/c.1/f1 2>/dev/null || true)
if echo "$f1" | grep -q 'gsi.ecm'; then
	log "already ecm, skip usb_switch"
	for n in ecm0 usb0 ncm0; do
		if [ -d "/sys/class/net/$n" ]; then
			ip link set "$n" up 2>/dev/null || true
			brctl show br-lan 2>/dev/null | grep -qw "$n" || brctl addif br-lan "$n" 2>/dev/null || true
		fi
	done
	exit 0
fi
log "usb_switch ecm+ffs"
serial=$(cat /sys/kernel/config/usb_gadget/g1/strings/0x409/serialnumber 2>/dev/null || true)
[ -n "$serial" ] || serial=$(cat /sys/class/android_usb/android0/iSerial 2>/dev/null || true)
case "$serial" in ''|*[!a-zA-Z0-9._-]*) log "target USB serial unavailable; no change";exit 1;; esac
sh /sbin/usb/compositions/usb_switch 0x19d2 0x1404 \
	"ecm,diag,serial,modem,mass_storage,ffs,dpl,qdss" \
	"$serial" >>"$LOG" 2>&1
sleep 2
for n in ecm0 usb0 ncm0 rndis0; do
	if [ -d "/sys/class/net/$n" ]; then
		ip link set "$n" up 2>/dev/null || true
		brctl show br-lan 2>/dev/null | grep -qw "$n" || brctl addif br-lan "$n" 2>/dev/null || true
		log "bridged $n"
	fi
done
log "f1=$(readlink /sys/kernel/config/usb_gadget/g1/configs/c.1/f1 2>/dev/null)"
log "nets=$(ls /sys/class/net | tr '\n' ' ')"
exit 0
