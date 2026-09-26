#!/bin/sh
# Explicit first-use initialization on the target; never copy another user's config.
set -eu
umask 077
[ "$(id -u)" = 0 ]
cd /data/u60-clash
# Existing installations explicitly opt in; updates never rewrite private config.
if [ "${1:-}" = --enable-ntp ] || [ "${1:-}" = --repair-ntp ];then
 exec 7>/tmp/u60-control.lock;flock -n 7 || { echo 'Another operation is running';exit 2; }
 [ -s config.yaml ] && [ -x mihomo ] || exit 1
 if grep -Eq '^ntp[[:space:]]*:' config.yaml && [ "$1" = --enable-ntp ];then
  echo 'NTP configuration already exists; retained it. Review enable, dialer-proxy and write-to-system in your private config.';exit 0
 fi
 backup="config-before-ntp-$(date +%s)-$$.private"
 cp -p config.yaml "$backup"
 trap 'rm -f config.yaml.ntp-next /tmp/u60-ntp-reply.$$' EXIT
 if [ "$1" = --repair-ntp ];then
  # Replace only our historical default in the top-level NTP block.
  # A private/custom NTP choice must never be rewritten by this migration.
  awk '
   /^ntp[[:space:]]*:/ { in_ntp=1 }
   in_ntp && /^[^[:space:]#]/ && !/^ntp[[:space:]]*:/ { in_ntp=0 }
   in_ntp && /^  server: time[.]apple[.]com[[:space:]]*$/ {
    print "  server: 162.159.200.1"; changed=1; next
   }
   { print }
   END { if (!changed) exit 3 }
  ' config.yaml > config.yaml.ntp-next || { echo 'Historical NTP default not found; configuration retained.';exit 0; }
 else
  cat config.yaml > config.yaml.ntp-next
  printf '\nntp:\n  enable: true\n  server: 162.159.200.1\n  port: 123\n  interval: 30\n  dialer-proxy: DIRECT\n  write-to-system: false\n' >> config.yaml.ntp-next
 fi
 ./mihomo -t -d /data/u60-clash -f /data/u60-clash/config.yaml.ntp-next >/dev/null 2>&1 || { echo 'Invalid candidate; original config retained';exit 1; }
 # Load only the local controller credential; never print or pass it in argv.
 secret=$(sed -n 's/^secret: *//p' config.yaml | tr -d '\"\047\r\n ')
 [ -n "$secret" ] || { echo 'Controller credential unavailable';exit 1; }
 reload() {
  printf 'header = "Authorization: Bearer %s"\n' "$secret" |
   curl -q -sS --noproxy '*' -K - --max-time 15 -X PUT -H 'Content-Type: application/json' --data '{"path":"/data/u60-clash/config.yaml"}' -o /tmp/u60-ntp-reply.$$ -w '%{http_code}' 'http://127.0.0.1:19090/configs?force=true'
 }
 [ "$(sha256sum config.yaml | cut -d ' ' -f 1)" = "$(sha256sum "$backup" | cut -d ' ' -f 1)" ] || { echo 'Configuration changed; candidate not applied';exit 1; }
 mv config.yaml.ntp-next config.yaml
 code=$(reload) || code=000
 case "$code" in 200|204) echo 'Mihomo NTP enabled without changing the system clock; verify real requests after synchronization.';;
 *) cp -p "$backup" config.yaml.ntp-next;mv config.yaml.ntp-next config.yaml;code=$(reload) || code=000
    case "$code" in 200|204) echo 'Reload failed; original configuration restored';; *) echo 'Original file restored but runtime rollback unconfirmed';; esac;exit 1;; esac
 exit 0
fi
[ "$#" = 0 ] || { echo 'Usage: setup-clash.sh [--enable-ntp|--repair-ntp]';exit 2; }
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
