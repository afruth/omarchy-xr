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
    def test_tron_available_without_images_or_imagemagick(self):
        with patch('environment.shutil.which', return_value=None), patch('environment._magick') as magick:
            snapshot = self.env.snapshot()
            self.assertFalse(snapshot['canImport'])
            self.assertEqual(snapshot['items'], [
                {'id':'builtin:tron', 'name':'Tron grid', 'kind':'procedural', 'thumbnail':''}
            ])
            self.env.set({'id':'builtin:tron', 'brightness':42, 'rotation':-90})
            self.assertEqual((self.root/'environment.tsv').read_text(), '42 -90 "builtin:tron" 1\n')
            self.assertTrue(self.env.config['animated'])
            self.assertEqual(list(self.env.library.iterdir()), [])
            magick.assert_not_called()

    def test_tron_animation_roundtrip_and_restart_without_assets(self):
        for animated in (False, True):
            with self.subTest(animated=animated):
                value = {'id':'builtin:tron', 'brightness':65, 'rotation':125, 'animated':animated}
                self.env.set(value)
                self.assertEqual(json.loads(self.env.profile.read_text()), value)
                # Restart with a different, empty library: a procedural sky has no asset path.
                restarted = Environment(self.root, self.root/'empty-library')
                self.assertEqual(restarted.config, value)
                self.assertEqual((self.root/'environment.tsv').read_text(),
                                 f'65 125 "builtin:tron" {int(animated)}\n')

    def test_tron_persisted_missing_animation_defaults_to_enabled(self):
        value = {'id':'builtin:tron', 'brightness':25, 'rotation':0}
        self.env.profile.write_text(json.dumps(value))
        restarted = Environment(self.root, self.root/'assets')
        self.assertEqual(restarted.config, {**value, 'animated':True})
        self.assertEqual((self.root/'environment.tsv').read_text(), '25 0 "builtin:tron" 1\n')

    def test_animation_requires_bool_and_rejection_preserves_settings(self):
        value = {'id':'builtin:tron', 'brightness':55, 'rotation':45, 'animated':False}
        self.env.set(value)
        profile = self.env.profile.read_text()
        renderer_config = (self.root/'environment.tsv').read_text()
        for identity in ('builtin:tron', ''):
            for invalid in (0, 1, None, 'false', [], {}, 0.0):
                with self.subTest(identity=identity, invalid=invalid):
                    with self.assertRaisesRegex(ValueError, 'animated'):
                        self.env.set({**value, 'id':identity, 'animated':invalid})
                    self.assertEqual(self.env.config, value)
                    self.assertEqual(self.env.profile.read_text(), profile)
                    self.assertEqual((self.root/'environment.tsv').read_text(), renderer_config)

    def test_leaving_tron_keeps_legacy_black_settings_and_protocol(self):
        self.env.set({'id':'builtin:tron', 'brightness':72, 'rotation':30, 'animated':False})
        self.env.set({**self.env.config, 'id':''})
        expected = {'id':'', 'brightness':72, 'rotation':30}
        self.assertEqual(self.env.config, expected)
        self.assertEqual(Environment(self.root, self.env.library).config, expected)
        self.assertEqual((self.root/'environment.tsv').read_text(), '72 30 ""\n')

    def test_tron_is_first_and_cannot_be_shadowed_by_library_record(self):
        for identity, name in [('panorama', 'A panorama'), ('builtin:tron', 'Fake Tron')]:
            folder = self.env.library / identity
            folder.mkdir()
            (folder/'sky.bmp').write_bytes(b'fixture')
            (folder/'asset.json').write_text(json.dumps({'id':identity, 'name':name}))
        self.assertEqual([item['name'] for item in self.env.items()], ['Tron grid', 'A panorama'])
        self.env.set({'id':'panorama', 'brightness':35, 'rotation':-15, 'animated':False})
        expected_path = json.dumps(str(self.env.library/'panorama/sky.bmp'))
        self.assertEqual((self.root/'environment.tsv').read_text(), f'35 -15 {expected_path}\n')
        self.assertNotIn('animated', self.env.config)
    @unittest.skipUnless(shutil.which('magick'),'ImageMagick unavailable')
    def test_import_deduplicates_and_preserves_original(self):
        import subprocess
        source=self.root/'sky [one].png'
        subprocess.run(['magick','-size','512x256','xc:navy',str(source)],check=True)
        original=source.read_bytes()
        identity=self.env.import_image(source)
        self.assertEqual(self.env.import_image(source),identity)
        self.assertEqual(source.read_bytes(),original)
        self.assertEqual(len(self.env.items()),2)
        bmp=(self.env.library/identity/'sky.bmp').read_bytes()
        self.assertEqual(struct.unpack_from('<ii',bmp,18),(512,256))
        self.env.set({'id':identity,'brightness':30,'rotation':0})
        self.assertIn('sky.bmp',(self.root/'environment.tsv').read_text())
        subprocess.run(['magick','-size','512x512','xc:navy',str(source)],check=True)
        with self.assertRaisesRegex(ValueError,'2:1'):self.env.import_image(source)
        self.assertEqual(len(self.env.items()),2)
        self.assertFalse(list(self.env.library.glob('.import-*')))
