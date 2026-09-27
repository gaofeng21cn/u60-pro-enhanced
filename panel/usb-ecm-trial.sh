#!/bin/sh
# Recovery is unqualified. Reject every writer before inspecting old state or
# consulting environment variables; stale transactions must not restart it.
case "${1:-status}" in
 status) exec /data/u60-panel/enable-usb-macnet.sh status ;;
 *) printf '%s\n' '{"ok":false,"message":"ECM 切换与恢复尚未通过实机验收，未修改 USB"}'; exit 1 ;;
esac
