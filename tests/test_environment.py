import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'studio'))
from environment import Environment

class EnvironmentTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.env=Environment(self.root,self.root/'assets')
    def tearDown(self): self.temp.cleanup()
    def test_settings_roundtrip_no_compositor(self):
        value={'id':'','brightness':42,'rotation':-90}
        self.env.set(value)
        self.assertEqual(Environment(self.root,self.root/'assets').config,value)
        self.assertEqual((self.root/'environment.tsv').read_text(),'42 -90 ""\n')
        before=self.env.profile.read_text()
        for bad in ({**value,'id':'../bad'},{**value,'brightness':float('nan')},{**value,'rotation':181},{**value,'brightness':True}):
            with self.assertRaises(ValueError): self.env.set(bad)
        self.assertEqual(self.env.profile.read_text(),before)
    def test_missing_asset_falls_back_to_black(self):
        self.env.profile.write_text(json.dumps({'id':'missing','brightness':25,'rotation':0}))
        self.assertEqual(Environment(self.root,self.root/'assets').config['id'],'')
    def test_import_dependency_error(self):
        with patch('environment.shutil.which',return_value=None):
            with self.assertRaisesRegex(RuntimeError,'ImageMagick'):self.env.import_image('/missing.jpg')
    @unittest.skipUnless(shutil.which('magick'),'ImageMagick unavailable')
    def test_import_deduplicates_and_preserves_original(self):
        import subprocess
        source=self.root/'sky [one].png'
        subprocess.run(['magick','-size','512x256','xc:navy',str(source)],check=True)
        original=source.read_bytes()
        identity=self.env.import_image(source)
        self.assertEqual(self.env.import_image(source),identity)
        self.assertEqual(source.read_bytes(),original)
        self.assertEqual(len(self.env.items()),1)
        bmp=(self.env.library/identity/'sky.bmp').read_bytes()
        self.assertEqual(struct.unpack_from('<ii',bmp,18),(512,256))
        self.env.set({'id':identity,'brightness':30,'rotation':0})
        self.assertIn('sky.bmp',(self.root/'environment.tsv').read_text())
        subprocess.run(['magick','-size','512x512','xc:navy',str(source)],check=True)
        with self.assertRaisesRegex(ValueError,'2:1'):self.env.import_image(source)
        self.assertEqual(len(self.env.items()),1)
        self.assertFalse(list(self.env.library.glob('.import-*')))
