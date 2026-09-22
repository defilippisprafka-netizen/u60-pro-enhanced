#!/usr/bin/env python3
import hashlib,importlib.util,pathlib,tempfile,unittest
R=pathlib.Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('prepare',R/'scripts/prepare.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Preparation(unittest.TestCase):
 def test_stock_patch_and_auth_route(self):
  examples={'index.html':b'<ul class="main-navigation-list"><script data-main="js/main">','js/main.js':b'require.config({paths:abc','js/config/ufi/U60Pro/menu.js':b'define(function(){return[abc'}
  for name,data in examples.items():
   patched=m.patch_web(name,data,m.sha(data));self.assertNotEqual(patched,data)
   with self.assertRaises(ValueError):m.patch_web(name,patched,m.sha(data))
  self.assertIn(b'requireLogin:!0',m.patch_web('js/config/ufi/U60Pro/menu.js',examples['js/config/ufi/U60Pro/menu.js'],m.sha(examples['js/config/ufi/U60Pro/menu.js'])))
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
