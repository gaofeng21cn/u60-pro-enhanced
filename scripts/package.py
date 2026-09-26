#!/usr/bin/env python3
"""Package only reviewed project files; no device backups or third-party binaries."""
import hashlib,importlib.util,pathlib,shutil,tarfile
R=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('prepare',R/'scripts/prepare.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
VERSION='v0.1.12-experimental';out=R/'dist'/('u60-pro-enhanced-'+VERSION)
if out.exists():raise SystemExit('Release directory exists; inspect it before rebuilding')
out.mkdir(parents=True)
for n in ['prepare.py']:shutil.copyfile(R/'scripts'/n,out/n)
for n in ['dependencies.json','factory-web.sha256','factory-web-B31.sha256','FACTORY-SHA256SUMS','FACTORY-SHA256SUMS-B31']:shutil.copyfile(R/'packaging'/n,out/n)
shutil.copyfile(R/'README.md',out/'README.md')
shutil.copytree(R/'docs',out/'docs');shutil.copytree(R/'licenses',out/'licenses')
macos=out/'macos';macos.mkdir();shutil.copyfile(R/'scripts/macos/u60-rndis.sh',macos/'u60-rndis.sh');(macos/'u60-rndis.sh').chmod(0o755)
for n in ['LICENSE','THIRD_PARTY_NOTICES.md']:shutil.copyfile(R/n,out/n)
i=out/'installer';p=i/'payload';data=p/'data';panel=data/'u60-panel';panel.mkdir(parents=True)
for f in sorted((R/'panel').glob('*.sh')):
 if f.name.endswith('-init.sh'):continue
 shutil.copyfile(f,panel/f.name);(panel/f.name).chmod(0o700)
for n in ['u60-panel','panel-control','panel-ubus','panel-wifi-crypto','panel-relay','panel-standby','panel-web','panel-web-control']:
 shutil.copyfile(R/'build'/n,panel/n);(panel/n).chmod(0o700)
for n,text in {'network-profile':'direct','tailscale-lan':'0','tailscale-mode':'tun','standby-mode':'normal','usb-role':'LAN'}.items():(panel/n).write_text(text+'\n')
for d in ['u60-clash/rules','tailscale/bin','u60-web/public']:(data/d).mkdir(parents=True)
clash=data/'u60-clash';shutil.copytree(R/'ui',clash/'ui')
shutil.copyfile(R/'scripts/portable/config.example.yaml',clash/'config.example.yaml')
for n,target,command in [('start.sh',clash,'network-profile.sh clash-start'),('tailscale-start.sh',data/'tailscale','tailscale-mode.sh start')]:
 f=target/n;f.write_text('#!/bin/sh\nexec /data/u60-panel/'+command+'\n');f.chmod(0o700)
for n in ['setup-clash.sh','setup-tailscale.sh']:
 shutil.copyfile(R/'scripts/portable'/n,panel/n);(panel/n).chmod(0o700)
web=data/'u60-web';shutil.copyfile(R/'web/factory/mount.sh',web/'mount.sh');(web/'mount.sh').chmod(0o700)
for local,remote in [('u60-web-advanced.js','js/u60-web-advanced.js'),('u60-enhanced.js','js/auth/u60-enhanced.js'),('u60-web-model.js','js/u60-web-model.js'),('u60-enhanced.html','tmpl/auth/u60-enhanced.html')]:
 dest=web/'public'/remote;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(R/'web/factory'/local,dest)
(web/'public/u60-extension-version.txt').write_text('20260926-network-profiles-21\n')
(p/'init').mkdir();(p/'boot').mkdir()
for local,remote in [('usb-isolate','u60-usb-isolate'),('usb-role','u60-usb-role'),('wifi-relay','u60-wifi-relay'),('standby','u60-standby'),('web','u60-web'),('usb-ncm-trial','u60-ncm-trial')]:
 shutil.copyfile(R/'panel'/(local+'-init.sh'),p/'init'/remote);(p/'init'/remote).chmod(0o700)
shutil.copyfile(R/'scripts/portable/portable-boot.sh',p/'boot/portable-boot.sh')
for n in ['check-device.sh','install-new-device.sh','upgrade-installed.sh','restore-boot.sh','deploy-from-computer.py']:shutil.copyfile(R/'scripts/portable'/n,i/n)
m.manifest(out);m.verify(out)
archive=out.parent/(out.name+'.tar.gz')
with tarfile.open(archive,'w:gz') as tar:
 for f in sorted(out.rglob('*')):
  if not f.is_file():continue
  info=tar.gettarinfo(str(f),arcname=out.name+'/'+f.relative_to(out).as_posix());info.uid=info.gid=0;info.uname=info.gname='';info.mtime=0
  with f.open('rb') as stream:tar.addfile(info,stream)
archive.with_name('SHA256SUMS.txt').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
print(archive)
