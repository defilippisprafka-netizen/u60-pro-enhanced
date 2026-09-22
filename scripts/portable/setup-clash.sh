#!/bin/sh
# Explicit first-use initialization on the target; never copy another user's config.
set -eu
umask 077
[ "$(id -u)" = 0 ]
cd /data/u60-clash
[ ! -e config.yaml ] || { echo 'Configuration already exists; nothing changed.';exit 1; }
[ -x mihomo ] && [ -s config.example.yaml ]
secret=$(od -An -N32 -tx1 /dev/urandom | tr -d ' \n')
[ "${#secret}" = 64 ]
# Secret stays in this shell and target 0600 configuration, never in argv or output.
while IFS= read -r line;do
 case "$line" in 'secret: ""') printf 'secret: "%s"\n' "$secret";; *) printf '%s\n' "$line";; esac
done < config.example.yaml > config.yaml.new
unset secret
chmod 600 config.yaml.new
if ! ./mihomo -t -d /data/u60-clash -f /data/u60-clash/config.yaml.new >/dev/null 2>&1;then
 rm -f config.yaml.new;echo 'Configuration validation failed.';exit 1
fi
mv config.yaml.new config.yaml
/data/u60-panel/network-profile.sh clash-start
echo 'Core initialized. Add your subscription in the authenticated enhanced web page, select a node, then enable proxy routing. Default node is DIRECT.'
