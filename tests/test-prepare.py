#!/usr/bin/env python3
import hashlib,importlib.util,pathlib,tempfile,unittest
R=pathlib.Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('prepare',R/'scripts/prepare.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Preparation(unittest.TestCase):
 def test_only_the_two_fingerprinted_firmware_variants_are_accepted(self):
  self.assertEqual(m.FIRMWARE[b'BD_FLYMODEMMU5250V1.0.0B28'],'B28')
  self.assertEqual(m.FIRMWARE[b'BD_CNMU5250V1.0.0B31'],'B31')
  self.assertNotIn(b'BD_CNMU5250V1.0.0B27',m.FIRMWARE)
 def test_stock_patch_and_auth_route(self):
  examples={'index.html':b'<ul class="main-navigation-list"><li>stock<ul><li>nested</li></ul></li></ul><script data-main="js/main">','js/main.js':b'require.config({paths:abc','js/config/ufi/U60Pro/menu.js':b'define(function(){return[abc'}
  for name,data in examples.items():
   patched=m.patch_web(name,data,m.sha(data));self.assertNotEqual(patched,data)
   with self.assertRaises(ValueError):m.patch_web(name,patched,m.sha(data))
  menu=m.patch_web('js/config/ufi/U60Pro/menu.js',examples['js/config/ufi/U60Pro/menu.js'],m.sha(examples['js/config/ufi/U60Pro/menu.js']))
  self.assertIn(b'requireLogin:!0',menu)
  self.assertEqual(menu.count(b'path:"auth/u60-enhanced"'),6)
 def test_menu_added_after_nested_stock_entries(self):
  data=b'<ul class="main-navigation-list"><li>stock<ul><li>nested</li></ul></li></ul><script data-main="js/main">'
  result=m.patch_web('index.html',data,m.sha(data))
  self.assertIn(b'nested</li></ul></li><li class="navigation-drawer -u60-enhanced"><div class="label">',result)
  self.assertIn(b'#u60_enhanced_clash',result)
  broken=b'<ul class="main-navigation-list"><script data-main="js/main">'
  with self.assertRaises(ValueError):m.patch_web('index.html',broken,m.sha(broken))
 def test_menu_icon_is_injected_when_factory_head_is_present(self):
  data=b'<head></head><ul class="main-navigation-list"><li>stock</li></ul><script data-main="js/main">'
  result=m.patch_web('index.html',data,m.sha(data))
  self.assertIn(b'id="u60-enhanced-navigation-style"',result)
  self.assertIn(b'data:image/svg+xml',result)
 def test_ambiguous_marker_refused(self):
  data=b'require.config({paths:require.config({paths:'
  with self.assertRaises(ValueError):m.patch_web('js/main.js',data,m.sha(data))
 def test_manifest_extra_and_symlink_refused(self):
  with tempfile.TemporaryDirectory() as td:
   root=pathlib.Path(td);(root/'safe').write_text('demo');m.manifest(root);m.verify(root)
   (root/'private').write_text('synthetic')
   with self.assertRaises(ValueError):m.verify(root)
   (root/'private').unlink();(root/'link').symlink_to(root/'safe')
   with self.assertRaises(ValueError):m.verify(root)
if __name__=='__main__':unittest.main()
