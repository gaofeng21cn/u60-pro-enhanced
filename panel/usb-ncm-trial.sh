#!/bin/sh
# NCM candidate transaction. It is intentionally separate from the qualified
# RNDIS/ECM observer and is never started by boot or the normal UI.
# Quarantine: the device trial lost ADB and its rollback did not recover it.
# Keep diagnostics available, but reject every mutation before invoking tools.
# There is deliberately no environment-variable or marker-file override.
case "${1:-status}" in
 status) ;;
 *) printf '%s\n' '{"ok":false,"message":"NCM 试运行已停用：ADB 恢复保护尚未验证，保持原厂 USB 组合"}'; exit 1;;
esac
set -u
umask 077

G=/sys/kernel/config/usb_gadget/g1
N=/sys/class/net
U=/sys/class/udc
R=/tmp/u60-ncm-trial
IP=${USB_IP:-ip}
SELF=$(readlink -f "$0")
TAB=$(printf '\t')
ADB_FFS=/dev/usb-ffs/adb
USB_COMPOSITION=${USB_COMPOSITION:-/sbin/usb_composition}
USB_NCM_COMPOSITION=${USB_NCM_COMPOSITION:-/data/u60-panel/usb-ncm-composition.sh}
TRIAL_INIT=${TRIAL_INIT:-/etc/init.d/u60-ncm-trial}

fail() { printf '{"ok":false,"message":"%s"}\n' "$1"; exit 1; }
valid_name() { case "$1" in ''|*[!a-zA-Z0-9._-]*) return 1;; esac; }
valid_target() { case "$1" in ../../../../usb_gadget/g1/functions/*) return 0;; esac; return 1; }
state() { [ -f "$R/state" ] && cat "$R/state" || printf idle; }
read_attr() { cat "$1" 2>/dev/null || true; }

owner_busy() {
 [ -e /tmp/usb_bind_in_progress ] && return 0
 command -v pidof >/dev/null 2>&1 || true
 pidof zte_ubus_bsp_usb >/dev/null 2>&1 && return 0
 ps | grep -E '[ /]zte_usb_switch( |$)' >/dev/null 2>&1 && return 0
 ps | grep -E '[ /]usb_composition( |$)' >/dev/null 2>&1 && return 0
 return 1
}

snapshot() {
 mkdir -p "$R/snapshot" || return 1
 : > "$R/snapshot/links"
 for cfg in "$G"/configs/c.*; do
  [ -d "$cfg" ] || continue
  cn=${cfg##*/}; valid_name "$cn" || return 1
  for link in "$cfg"/f*; do
   [ -L "$link" ] || continue
   fn=${link##*/}; valid_name "$fn" || return 1
   target=$(readlink "$link"); valid_target "$target" || return 1
   printf '%s\t%s\t%s\n' "$cn" "$fn" "$target" >> "$R/snapshot/links" || return 1
  done
 done
 udc=$(read_attr "$G/UDC"); valid_name "$udc" || return 1
 printf '%s\n' "$udc" > "$R/snapshot/udc"
 for attr in idVendor idProduct bcdUSB bcdDevice bDeviceClass; do
  if [ -f "$G/$attr" ]; then read_attr "$G/$attr" > "$R/snapshot/$attr"; fi
 done
 for attr in bmAttributes MaxPower; do
  if [ -f "$G/configs/c.1/$attr" ]; then read_attr "$G/configs/c.1/$attr" > "$R/snapshot/config-$attr"; fi
 done
 for attr in "$G"/strings/0x409/* "$G"/configs/c.1/strings/0x409/* "$G"/os_desc/use "$G"/os_desc/b_vendor_code "$G"/os_desc/qw_sign; do
  [ -f "$attr" ] || continue
  rel=${attr#"$G"/}
  case "$rel" in *[!a-zA-Z0-9_./-]*) return 1;; esac
  mkdir -p "$R/snapshot/attrs/$(dirname "$rel")" || return 1
  read_attr "$attr" > "$R/snapshot/attrs/$rel" || return 1
 done
}

restore() {
 [ -s "$R/snapshot/links" ] || return 1
 udc=$(read_attr "$R/snapshot/udc"); valid_name "$udc" || return 1
 printf '\n' > "$G/UDC" || return 1
 for cfg in "$G"/configs/c.*; do
  [ -d "$cfg" ] || continue
  for link in "$cfg"/f*; do [ -L "$link" ] && rm "$link" || true; done
 done
 while IFS="$TAB" read -r cn fn target; do
  valid_name "$cn" && valid_name "$fn" && valid_target "$target" || return 1
  [ -d "$G/configs/$cn" ] || mkdir -p "$G/configs/$cn" || return 1
  ln -s "$target" "$G/configs/$cn/$fn" || return 1
 done < "$R/snapshot/links"
 for attr in idVendor idProduct bcdUSB bcdDevice bDeviceClass; do
  [ -f "$R/snapshot/$attr" ] || continue
  cat "$R/snapshot/$attr" > "$G/$attr" || return 1
 done
 for attr in "$R"/snapshot/config-MaxPower "$R"/snapshot/config-bmAttributes; do
  [ -f "$attr" ] || continue
  name=${attr##*/}; name=${name#config-}
  cat "$attr" > "$G/configs/c.1/$name" || return 1
 done
 for attr in "$R"/snapshot/attrs/strings/0x409/* "$R"/snapshot/attrs/configs/c.1/strings/0x409/* "$R"/snapshot/attrs/os_desc/*; do
  [ -f "$attr" ] || continue
  rel=${attr#"$R/snapshot/attrs/"}
  [ -f "$G/$rel" ] || return 1
  cat "$attr" > "$G/$rel" || return 1
 done
 printf '%s\n' "$udc" > "$G/UDC" || return 1
 [ "$(read_attr "$G/UDC")" = "$udc" ] || return 1
 : > "$R/readback-links"
 for cfg in "$G"/configs/c.*; do
  [ -d "$cfg" ] || continue
  cn=${cfg##*/}
  for link in "$cfg"/f*; do
   [ -L "$link" ] || continue
   printf '%s\t%s\t%s\n' "$cn" "${link##*/}" "$(readlink "$link")" >> "$R/readback-links" || return 1
  done
 done
 cmp -s "$R/snapshot/links" "$R/readback-links" || return 1
 for attr in "$G"/strings/0x409/* "$G"/configs/c.1/strings/0x409/* "$G"/os_desc/use "$G"/os_desc/b_vendor_code "$G"/os_desc/qw_sign; do
  [ -f "$attr" ] || continue
  rel=${attr#"$G"/}; [ -f "$R/snapshot/attrs/$rel" ] || return 1
  [ "$(read_attr "$attr")" = "$(read_attr "$R/snapshot/attrs/$rel")" ] || return 1
 done
 [ "$(read_attr "$U/$udc/state")" = configured ] || return 1
 [ -e "$ADB_FFS/ep0" ] || return 1
 pidof adbd >/dev/null 2>&1 || return 1
 found_adb=false
 for link in "$G"/configs/c.*/f*; do case "$(readlink "$link" 2>/dev/null || true)" in */functions/ffs.adb) found_adb=true;; esac; done
 [ "$found_adb" = true ] || return 1
 printf restored > "$R/state"
}

ncm_iface() {
 name=$(read_attr "$G/functions/ncm.0/ifname")
 valid_name "$name" && [ -e "$N/$name" ] || return 1
 printf '%s' "$name"
}

do_start() {
 [ -d "$G/functions/ncm.0" ] || fail '设备没有 NCM function'
 [ -e "$G/configs/c.1/f1" ] || fail '原厂 configuration 不完整'
 [ "$(readlink "$G/configs/c.1/f1" 2>/dev/null || true)" = ../../../../usb_gadget/g1/functions/gsi.rndis ] || fail '当前 USB 不是原厂 RNDIS 组合'
 if [ "$(uname -r 2>/dev/null || true)" != 5.15.194-perf ] || [ "$(ubus -t 5 call zwrt_zte_mdm.api get_zwrt_common_info '{}' 2>/dev/null | jsonfilter -e '@.wa_inner_version' 2>/dev/null || true)" != BD_CNMU5250V1.0.0B31 ]; then fail '仅支持已核对的 B31 设备'; fi
 udc_now=$(read_attr "$G/UDC"); valid_name "$udc_now" || fail 'USB 未绑定'
 [ "$(read_attr "$U/$udc_now/state")" = configured ] || fail 'UDC 尚未完成枚举'
 [ -e "$ADB_FFS/ep0" ] || fail 'ADB FunctionFS 尚未就绪'
 pidof adbd >/dev/null 2>&1 || fail 'adbd 未运行'
 [ "$(state)" = idle ] || [ "$(state)" = restored ] || fail '已有 NCM 试运行，请先恢复'
 owner_busy && fail '原厂 USB owner 正在运行，拒绝切换以保护 ADB；需维护模式和 Wi-Fi 恢复通道'
 mkdir -p "$R" || fail '无法建立 NCM 事务目录'
 exec 9>"$R/lock"; flock -n 9 || fail 'USB 操作正在执行'
 snapshot || fail '无法保存完整 USB 组合'
 [ -x "$USB_COMPOSITION" ] || fail '原厂 USB owner 不可用'
 [ -x "$USB_NCM_COMPOSITION" ] || fail '908C NCM composition 尚未安装'
 adb=false
 for f in "$G"/configs/c.*/f*; do case "$(readlink "$f" 2>/dev/null || true)" in */functions/ffs.adb) adb=true;; esac; done
 [ "$adb" = true ] || fail '当前组合没有 ADB'
 printf pending > "$R/state"; rm -f "$R/confirmed" "$R/cancel"
 # The supervisor is a separate procd instance and must acquire the lock
 # itself. Keeping the caller's descriptor open here would make a correct
 # procd supervisor reject its own transaction.
 exec 9>&-
 if [ -x "$TRIAL_INIT" ]; then
  "$TRIAL_INIT" start || fail '无法启动 NCM procd 监督服务'
 else
  nohup sh "$SELF" supervise </dev/null >"$R/log" 2>&1 &
 fi
 printf '%s\n' '{"ok":true,"message":"NCM 试运行已启动；必须先验证 Mac 枚举、DHCP、HTTPS 和 ADB 恢复"}'
}

supervise() {
 # procd starts this process independently of the shell that requested the
 # trial, so it cannot rely on an inherited descriptor. Hold the transaction
 # lock for the whole supervised window; restore waits for this lock before
 # rebuilding the factory composition.
 exec 9>"$R/lock" || exit 1
 flock -n 9 || exit 1
 [ "$(state)" = pending ] || exit 1
 trap 'restore || printf failed > "$R/state"' EXIT
 sleep 2
 owner_busy && fail '原厂 USB owner 发生并发切换'
 # The factory composition directory is read-only on B31. The project-owned
 # composition uses the same ConfigFS owner contract without touching firmware.
 "$USB_NCM_COMPOSITION" n n y || fail 'NCM composition 未能切换'
 iface=''
 n=0
 while [ "$n" -lt 20 ]; do
  iface=$(ncm_iface 2>/dev/null || true)
  [ -n "$iface" ] && break
  sleep 1; n=$((n+1))
 done
 [ -n "$iface" ] || fail 'NCM 网口未出现'
 "$IP" link set dev "$iface" master br-lan || fail 'NCM 未加入内网'
 "$IP" link set dev "$iface" up || fail 'NCM 网口无法启动'
 n=0
 while [ "$n" -lt 120 ]; do
  [ ! -e "$R/cancel" ] || exit 0
  if [ -f "$R/confirmed" ] && [ "$(read_attr "$N/$iface/carrier")" = 1 ]; then
   master=$(readlink -f "$N/$iface/master" 2>/dev/null || true)
 [ "${master##*/}" = br-lan ] && { printf active > "$R/state"; trap - EXIT; exit 0; }
  fi
  sleep 1; n=$((n+1))
 done
}

do_confirm() {
 [ "$(state)" = pending ] || fail '没有待确认的 NCM 试运行'
 iface=$(ncm_iface) || fail 'NCM 网口未知'
 [ "$(read_attr "$N/$iface/carrier")" = 1 ] || fail 'Mac NCM 链路未建立'
 master=$(readlink -f "$N/$iface/master" 2>/dev/null || true); [ "${master##*/}" = br-lan ] || fail 'NCM 未接入内网'
 touch "$R/confirmed"; n=0
 while [ "$n" -lt 5 ] && [ "$(state)" = pending ]; do sleep 1; n=$((n+1)); done
 [ "$(state)" = active ] || fail 'NCM 试运行未通过业务确认'
 printf '%s\n' '{"ok":true,"message":"已保留本次 NCM；重启或显式恢复会回到原厂 USB 组合"}'
}

do_restore() {
 [ -d "$R/snapshot" ] || fail '没有 NCM 试运行快照'
 touch "$R/cancel"
 [ ! -x "$TRIAL_INIT" ] || "$TRIAL_INIT" stop >/dev/null 2>&1 || true
 n=0; while [ "$n" -lt 10 ]; do
  exec 8>"$R/lock"; flock -n 8 && break
  sleep 1; n=$((n + 1))
 done
 flock -n 8 || fail 'NCM 试运行尚未结束'
 restore || fail 'NCM 恢复原厂组合失败'
 rm -f "$R/confirmed" "$R/cancel"
 printf '%s\n' '{"ok":true,"message":"已恢复原厂 RNDIS、ADB 和诊断组合"}'
}

case "${1:-status}" in
 status)
  busy=false; owner_busy && busy=true
  present=false; [ -d "$G/functions/ncm.0" ] && present=true
  printf '{"ok":true,"state":"%s","owner_busy":%s,"ncm_present":%s}\n' "$(state)" "$busy" "$present";;
 start) do_start;;
 confirm) do_confirm;;
 restore) do_restore;;
 supervise) supervise;;
 *) fail '用法：status | start | confirm | restore';;
esac
