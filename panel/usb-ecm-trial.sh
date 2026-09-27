#!/bin/sh
# The old maintenance transaction is retired. Status delegates to the formal
# owner; every legacy writer remains closed so stale transactions cannot bypass
# the guarded user-facing switch.
case "${1:-status}" in
 status) exec /data/u60-panel/enable-usb-macnet.sh status ;;
 *) printf '%s\n' '{"ok":false,"message":"旧 ECM 试验入口已停用；请使用网络中的 Mac USB 联网正式开关"}'; exit 1 ;;
esac
