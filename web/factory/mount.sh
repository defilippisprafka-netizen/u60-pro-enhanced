#!/bin/sh
# Read-only lower layers; stock ZTE partition is never remounted writable.
set -eu
root=/usr/zte_web/web
layer=/data/u60-web/public
view=/tmp/u60-webroot
mounted() { awk -v target="$1" '$2==target {found=1} END {exit !found}' /proc/mounts; }
case "${1:-}" in
 start)
  [ -r "$layer/u60-extension-version.txt" ] || exit 1
  if mounted "$root"; then
   [ "$(cat "$root/u60-extension-version.txt" 2>/dev/null)" = "$(cat "$layer/u60-extension-version.txt")" ] || exit 1
   exit 0
  fi
  # OTA/firmware changes fail closed to the original stock website.
  (cd "$root"; sha256sum -c /data/u60-web/factory.sha256 >/dev/null 2>&1) || exit 1
  mkdir -p "$view"
  mounted "$view" || mount -t overlay overlay -o "ro,lowerdir=$layer:$root" "$view"
  if ! mount -o bind "$view" "$root"; then umount "$view";exit 1;fi
  ;;
 stop)
  if mounted "$root"; then
   [ -f "$root/u60-extension-version.txt" ] || exit 1
   umount "$root"
  fi
  if mounted "$view"; then umount "$view";fi
  ;;
 *) exit 2;;
esac
