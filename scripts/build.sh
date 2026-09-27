#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
ZIG=${ZIG:-zig}
GO=${GO:-go}
[ "$("$ZIG" version)" = 0.14.1 ] || {
 echo 'Build requires Zig 0.14.1; set ZIG to that executable. No artifacts changed.' >&2
 exit 1
}
mkdir -p build/link
cc() { "$ZIG" cc -target aarch64-linux-musl -O2 -s -Wall -Wextra "$@"; }
cc -static -D__user= -I sdk -I panel/vendor panel/u60-panel.c panel/vendor/cJSON.c panel/vendor/qrcodegen.c -lm -o build/u60-panel
for name in panel-control panel-relay;do
 cc -static -I panel/vendor "panel/$name.c" panel/vendor/cJSON.c -lm -o "build/$name"
done
for item in 'ubus libubus.so.20230605' 'ztecrypto libztecrypto.so' 'uci libuci.so';do
 set -- $item
 cc -shared -fPIC -nostdlib sdk/stub.c "-Wl,-soname,$2" -o "build/link/lib$1.so"
done
for name in panel-ubus panel-web;do
 cc -dynamic -I sdk/include -I panel/vendor "panel/$name.c" panel/vendor/cJSON.c -ldl -Wl,--no-as-needed -L build/link -lubus -Wl,--dynamic-linker=/lib/ld-musl-aarch64.so.1 -o "build/$name"
done
cc -dynamic -I panel/vendor panel/panel-wifi-crypto.c panel/vendor/cJSON.c -ldl -L build/link -Wl,--no-as-needed -lztecrypto -Wl,--dynamic-linker=/lib/ld-musl-aarch64.so.1 -o build/panel-wifi-crypto
cc -dynamic panel/panel-standby.c -ldl -L build/link -Wl,--no-as-needed -luci -Wl,--dynamic-linker=/lib/ld-musl-aarch64.so.1 -o build/panel-standby
(cd web/controller; CGO_ENABLED=0 GOOS=linux GOARCH=arm64 "$GO" build -trimpath -ldflags '-s -w' -o ../../build/panel-web-control .)
echo 'Built ARM64 programs in build/'
