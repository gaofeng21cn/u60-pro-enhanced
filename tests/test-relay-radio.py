#!/usr/bin/env python3
"""Regress B28 false CSA success: daemon state is not driver channel state."""
import pathlib,tempfile,subprocess,os
R=pathlib.Path(__file__).resolve().parents[1]
s=(R/'panel/wifi-relay.sh').read_text();f=s[s.index('radio_args()'):s.index('allowed()')]
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td)
 pre='''set -eu
ROOT=$RUN
setting() { echo 1; }
flock() { [ "$1" = -n ] && [ "$2" = 7 ]; }
sleep() { :; }
iw() { printf 'channel 1 (%s MHz), width: 20 MHz\\n' "$(cat "$RUN/$2.driver")"; }
ap_control() {
 i=$1;c=$2;shift 2
 case "$c" in
 status) cat "$RUN/$i";;
 chan_switch)
 # B28 reports target while driver remains old; B31 native CSA moves both.
 [ ! -f "$RUN/native" ] || printf '%s\\n' "$2" > "$RUN/$i.driver"
 sed "s/^freq=.*/freq=$2/" "$RUN/$i" > "$RUN/$i.next";mv "$RUN/$i.next" "$RUN/$i";echo OK;;
 disable) sed 's/^state=.*/state=DISABLED/' "$RUN/$i" > "$RUN/$i.next";mv "$RUN/$i.next" "$RUN/$i";echo OK;;
 set)
 [ ! -f "$RUN/reject" ] || { echo FAIL;return; }
 [ "$(sed -n 's/^state=//p' "$RUN/$i")" = DISABLED ] || { echo FAIL;return; }
 printf '%s %s %s\\n' "$i" "$1" "$2" >> "$RUN/sets"
 if [ "$1" = channel ];then
  freq=$((5000+$2*5));[ "$i" != wlan0 ] || freq=$((2407+$2*5))
  sed "s/^freq=.*/freq=$freq/" "$RUN/$i" > "$RUN/$i.next";mv "$RUN/$i.next" "$RUN/$i"
 fi;echo OK;;
 enable)
 sed 's/^state=.*/state=ENABLED/' "$RUN/$i" > "$RUN/$i.next";mv "$RUN/$i.next" "$RUN/$i"
 [ -f "$RUN/stuck" ] || sed -n 's/^freq=//p' "$RUN/$i" > "$RUN/$i.driver"
 echo OK;;
 esac
}
'''
 f=f.replace('/tmp/u60-wifi-band.action',str(p/'lock'))
 def reset():
  (p/'wlan0').write_text('state=ENABLED\nfreq=2412\nsecondary_channel=1\nvht_oper_chwidth=0\nvht_oper_centr_freq_seg0_idx=0\n')
  (p/'wlan2').write_text('state=ENABLED\nfreq=5180\nsecondary_channel=1\nvht_oper_chwidth=2\nvht_oper_centr_freq_seg0_idx=50\n')
  (p/'wlan0.driver').write_text('2412');(p/'wlan2.driver').write_text('5180')
 def run(c):return subprocess.run(['sh','-c',pre+f+'\n'+c],env=dict(os.environ,RUN=td),text=True,capture_output=True,timeout=15)
 reset()
 r=run('radio_align 2437');assert r.returncode==0,r.stderr
 assert (p/'wlan0.driver').read_text().strip()=='2437','false success: daemon moved but driver did not'
 count=len((p/'sets').read_text().splitlines());assert run('radio_align 2437').returncode==0
 assert len((p/'sets').read_text().splitlines())==count,'unchanged healthy channel must not restart'
 assert run('radio_align 5220').returncode==0
 assert (p/'wlan2.driver').read_text().strip()=='5220'
 assert (p/'radio-wlan0').read_text().strip()=='2412 0 1 0'
 assert (p/'radio-wlan2').read_text().strip()=='5180 2 1 50'
 assert run('radio_align 5260').returncode!=0 and run('radio_align "2437;reboot"').returncode!=0
 assert run('radio_restore').returncode==0
 assert not (p/'radio-wlan0').exists() and not (p/'radio-wlan2').exists()
 assert (p/'wlan0.driver').read_text().strip()=='2412' and (p/'wlan2.driver').read_text().strip()=='5180'
 assert 'wlan2 vht_oper_chwidth 2' in (p/'sets').read_text()
 # A daemon/driver mismatch even on the requested channel must not be a no-op.
 (p/'wlan0.driver').write_text('2462');assert run('radio_align 2412').returncode==0
 assert (p/'wlan0.driver').read_text().strip()=='2412'
 (p/'stuck').touch();assert run('radio_align 2437').returncode!=0,'driver mismatch must fail'
 (p/'stuck').unlink();reset();(p/'reject').touch()
 assert run('radio_align 2437').returncode!=0
 assert (p/'radio-wlan0').exists() and 'state=ENABLED' in (p/'wlan0').read_text(),'failed setter must recover AP'
 (p/'reject').unlink();reset();assert run('radio_align 5220').returncode==0
 (p/'wlan2').write_text((p/'wlan2').read_text().replace('ENABLED','DISABLED'))
 assert run('radio_restore').returncode==0 and 'state=ENABLED' in (p/'wlan2').read_text()
 assert (p/'wlan2.driver').read_text().strip()=='5180' and not (p/'radio-wlan2').exists()
 reset();assert run('radio_align 5220').returncode==0
 (p/'wlan2').write_text((p/'wlan2').read_text().replace('ENABLED','DISABLED'));(p/'reject').touch()
 assert run('radio_restore').returncode!=0 and (p/'radio-wlan2').exists()
 (p/'reject').unlink();reset();assert run('radio_align 5220').returncode==0
 (p/'wlan2').write_text((p/'wlan2').read_text().replace('ENABLED','DISABLED'));(p/'wifi-5g-policy').write_text('off')
 assert run('radio_restore').returncode==0 and 'state=DISABLED' in (p/'wlan2').read_text() and (p/'radio-wlan2').exists()
 (p/'wifi-5g-policy').unlink();assert run('radio_restore').returncode==0 and not (p/'radio-wlan2').exists()
 reset();(p/'native').touch();(p/'compat-mode').write_text('b31-ui-first\n')
 count=len((p/'sets').read_text().splitlines());assert run('radio_align 5745').returncode==0
 assert (p/'wlan2.driver').read_text().strip()=='5745' and len((p/'sets').read_text().splitlines())==count
 assert run('radio_restore').returncode==0 and (p/'wlan2.driver').read_text().strip()=='5180'
 print('PASS: real driver confirmation, B28 stopped-BSS and B31 native CSA, two bands, no-op, original geometry restore, malformed/DFS rejection and failure recovery')
