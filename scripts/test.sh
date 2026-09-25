#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
for source in tests/test-theme.c tests/test-menu-layout.c tests/test-advanced-control.c tests/test-radio-tools.c tests/test-charge.c tests/test-power-role.c tests/test-web-policy.c tests/test-standby-policy.c tests/test-relay.c;do
 name=$(basename "$source" .c)
 cc -O1 -I panel -I panel/vendor "$source" panel/vendor/cJSON.c -lm -o "build/tests/$name"
 "build/tests/$name"
done
for source in tests/test-*.py tests/control-test.py;do python3 "$source";done
for source in tests/test-web-*.js;do node "$source";done
for source in web/factory/u60-enhanced.js web/factory/u60-web-advanced.js web/factory/u60-web-model.js;do node --check "$source";done
(cd web/controller; go test ./...)
for source in panel/*.sh scripts/*.sh scripts/portable/*.sh;do sh -n "$source";done
echo 'PASS: host regression suite (not a substitute for new-device physical tests)'
