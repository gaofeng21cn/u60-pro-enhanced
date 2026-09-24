#!/usr/bin/env python3
"""Prepare a device-specific installer locally. Never publish the prepared folder."""
import argparse,datetime,gzip,hashlib,io,json,pathlib,re,shutil,subprocess,tarfile,urllib.request
ROOT=pathlib.Path(__file__).resolve().parent
FIRMWARE={b'BD_FLYMODEMMU5250V1.0.0B28':'B28',b'BD_CNMU5250V1.0.0B31':'B31'}
WEB={'index.html':('<ul class="main-navigation-list">','<ul class="main-navigation-list">\n<li class="navigation-drawer -u60-enhanced"><a href="#u60_enhanced" class="parent-link link">增强功能</a></li>'),
 'js/main.js':('require.config({paths:','require.config({urlArgs:"u60=20260924-clash-ux-2",paths:'),
 'js/config/ufi/U60Pro/menu.js':('return[','return[{hash:"#u60_enhanced",path:"auth/u60-enhanced",requireLogin:!0,checkSIMStatus:!1},')}
def sha(data):return hashlib.sha256(data).hexdigest()
def patch_web(name,data,expected):
 if sha(data)!=expected:raise ValueError('Factory web fingerprint mismatch: '+name)
 text=data.decode('utf-8');old,new=WEB[name]
 if text.count(old)!=1:raise ValueError('Unexpected factory web layout')
 if name=='index.html':
  # Preserve every stock menu, append our entry after nested submenus.
  start=text.index(old)+len(old);depth=1;end=None
  for match in re.finditer(r'</?ul\b[^>]*>',text[start:]):
   depth += -1 if match.group().startswith('</') else 1
   if depth==0:end=start+match.start();break
  if end is None:raise ValueError('Unclosed stock navigation')
  text=text[:end]+'<li class="navigation-drawer -u60-enhanced"><a href="#u60_enhanced" class="parent-link link">增强功能</a></li>'+text[end:]
  if text.count('data-main="js/main"')!=1:raise ValueError('Unexpected main script')
  text=text.replace('data-main="js/main"','data-main="js/main.js?u60=20260924-clash-ux-2"')
 else:text=text.replace(old,new,1)
 return text.encode('utf-8')
def verify(root):
 expected={'SHA256SUMS'}
 for line in (root/'SHA256SUMS').read_text().splitlines():
  h,n=line.split('  ',1);p=pathlib.PurePosixPath(n)
  if n in expected or p.is_absolute() or '..' in p.parts:raise ValueError('Unsafe manifest')
  expected.add(n);f=root/n
  if f.is_symlink() or sha(f.read_bytes())!=h:raise ValueError('Package checksum mismatch')
 actual=set()
 for p in root.rglob('*'):
  if p.is_symlink():raise ValueError('Symlinks are not permitted')
  if p.is_file():actual.add(p.relative_to(root).as_posix())
 if actual!=expected:raise ValueError('Extra or missing package files')
def download(spec):
 with urllib.request.urlopen(spec['url'],timeout=120) as response:data=response.read(200_000_001)
 if len(data)>200_000_000 or sha(data)!=spec['sha256']:raise ValueError('Dependency checksum mismatch')
 return data
def write(root,name,data,executable=False,public=False):
 p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);p.chmod(0o700 if executable else 0o644 if public else 0o600)
def manifest(root):
 (root/'SHA256SUMS').write_text(''.join(sha(p.read_bytes())+'  '+p.relative_to(root).as_posix()+'\n' for p in sorted(root.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--adb',default=shutil.which('adb'));p.add_argument('--serial');p.add_argument('--output',type=pathlib.Path,default=ROOT.parent/'u60-prepared-private');a=p.parse_args()
 if not a.adb:p.error('Install official platform-tools and provide --adb')
 if a.output.exists():p.error('Output already exists; choose a new empty output path')
 verify(ROOT)
 devices=subprocess.check_output([a.adb,'devices'],text=True,timeout=15)
 ids=[l.split()[0] for l in devices.splitlines()[1:] if len(l.split())==2 and l.split()[1]=='device']
 if not a.serial and len(ids)!=1:p.error('Connect exactly one device or specify --serial')
 serial=a.serial or ids[0]
 if serial not in ids:p.error('Selected device is not ready')
 adb=[a.adb,'-s',serial,'exec-out']
 # These are fixed read-only paths. No credentials, account state or configuration is read.
 fw=subprocess.check_output(adb+['sh','-c',"ubus -t 5 call zwrt_zte_mdm.api get_zwrt_common_info '{}' | jsonfilter -e '@.wa_inner_version'"],timeout=15).strip()
 variant=FIRMWARE.get(fw)
 if not variant:p.error('Only the verified mainland B28 and B31 firmware is supported')
 imei=subprocess.check_output(adb+['sh','-c',"ubus -t 5 call zwrt_web device_info '{}' | jsonfilter -e '@.imei'"],timeout=15).strip()
 if not re.fullmatch(rb'[0-9]{15}',imei):p.error('Cannot bind package to a valid device identity')
 stock={}
 web_manifest=ROOT/('factory-web.sha256' if variant=='B28' else 'factory-web-B31.sha256')
 for line in web_manifest.read_text().splitlines():
  h,n=line.split('  ',1)
  if n not in WEB:raise ValueError('Unexpected web source')
  stock[n]=patch_web(n,subprocess.check_output(adb+['cat','/usr/zte_web/web/'+n],timeout=15),h)
 output=a.output.resolve();shutil.copytree(ROOT/'installer',output);output.chmod(0o700)
 try:
  data_root=output/'payload/data'
  write(output,'FACTORY-SHA256SUMS',(ROOT/('FACTORY-SHA256SUMS' if variant=='B28' else 'FACTORY-SHA256SUMS-B31')).read_bytes())
  release_time=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S')
  write(output,'RELEASE-ID',('u60-pro-'+variant+'-'+release_time+'\n').encode())
  write(output,'TARGET-IDENTITY-SHA256',(sha(b'u60-imei-v1:'+imei)+'\n').encode())
  write(data_root/'u60-web','factory.sha256',web_manifest.read_bytes())
  if variant=='B31':write(data_root/'u60-panel','compat-mode',b'b31-ui-first\n')
  for n,data in stock.items():write(data_root/'u60-web/public',n,data,public=True)
  for name,spec in json.loads((ROOT/'dependencies.json').read_text()).items():
   print('Downloading and verifying '+name,flush=True);data=download(spec)
   if spec['format']=='tailscale-tar':
    with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
     for binary in ('tailscale','tailscaled'):
      member=archive.getmember('tailscale_1.102.4_arm64/'+binary)
      if not member.isfile() or member.size>150_000_000:raise ValueError('Unexpected dependency member')
      write(data_root,'tailscale/bin/'+binary,archive.extractfile(member).read(),True)
   else:
    if spec['format']=='gzip':data=gzip.decompress(data)
    write(data_root,spec['target'],data,spec['format']=='gzip')
  manifest(output);verify(output)
 except Exception:
  print('Preparation incomplete; do not install this folder. Remove it and retry.');raise
 print('Prepared locally: '+str(output))
 print('Review README, then run this folder\'s deploy-from-computer.py check, install and start in order.')
if __name__=='__main__':main()
