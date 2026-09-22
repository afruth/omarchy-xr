import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'studio'))
from sdk import SDK


def load_script(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT / 'scripts' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release = load_script('package-release')
setup = load_script('package-setup')


class ReleaseTests(unittest.TestCase):
    def test_staged_package_is_complete_without_developer_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            archive = base / 'sdk.zip'
            with zipfile.ZipFile(archive, 'w') as sdk:
                sdk.writestr('x86_64/libglasses.so', b'preserved vendor runtime')
                sdk.writestr('include/private.h', b'do not distribute')
                sdk.writestr('../../escaped', b'never extract')
                sdk.writestr('x86_64/libcarina_vio.so', b'excluded')
            renderer = base / 'renderer'
            renderer.write_bytes(b'renderer')
            stage = base / 'stage'
            with patch.object(release, 'SDK_SHA256', hashlib.sha256(archive.read_bytes()).hexdigest()):
                runtime = release.install_tree(stage, renderer, archive)
            self.assertEqual(runtime.read_bytes(), b'preserved vendor runtime')
            self.assertTrue(os.access(stage / 'usr/bin/omarchy-xr', os.X_OK))
            self.assertTrue(os.access(stage / 'usr/lib/omarchy-xr/omarchy-xr-display', os.X_OK))
            self.assertFalse((base / 'escaped').exists())
            self.assertEqual(sorted(p.name for p in runtime.parent.iterdir()), ['libglasses.so'])
            manifest = json.loads((stage / 'usr/share/omarchy-xr/plugin/manifest.json').read_text())
            self.assertEqual(manifest['license'], 'LicenseRef-Omarchy-XR')
            for entry in manifest['entryPoints'].values():
                self.assertTrue((stage / 'usr/share/omarchy-xr/plugin' / entry).is_file())
            self.assertTrue((stage / 'usr/share/licenses/omarchy-xr-bin/WAYLAND-PROTOCOLS.txt').is_file())
            self.assertEqual([p.name for p in stage.iterdir()], ['usr'])

    def test_rejects_unreviewed_sdk_before_staging_any_files(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            archive = base / 'sdk.zip'
            archive.write_bytes(b'wrong SDK')
            with self.assertRaisesRegex(ValueError, 'differs'):
                release.install_tree(base / 'stage', base / 'renderer', archive)
            self.assertFalse((base / 'stage').exists())

    def test_runtime_lookup_and_licence_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            library = base / 'libglasses.so'
            terms = base / 'LICENSE'
            terms.write_text('release terms')
            sdk = SDK(base / 'state')
            with patch('sdk.PACKAGED_LIBRARY', library), patch('sdk.PACKAGED_TERMS', terms), patch.dict(os.environ, {}, clear=True):
                self.assertNotEqual(sdk.library(), library)
                library.write_bytes(b'packaged SDK')
                self.assertEqual(sdk.library(), library)
                self.assertFalse(sdk.license_accepted())
                with patch.object(setup, 'SHARE', base):
                    setup.accept_terms(base / 'state', assume=True)
                self.assertTrue(sdk.license_accepted())
                terms.write_text('changed terms')
                self.assertFalse(sdk.license_accepted())
                with patch.dict(os.environ, {'VITURE_SDK_LIBRARY': '/developer/sdk.so'}):
                    self.assertEqual(sdk.library(), Path('/developer/sdk.so'))

    def test_declined_terms_do_not_create_acceptance(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'LICENSE').write_text('terms')
            (base / 'PRIVACY.md').write_text('privacy')
            with patch.object(setup, 'SHARE', base), patch('sys.stdin.isatty', return_value=True), patch('builtins.input', return_value='no'), patch('builtins.print'):
                with self.assertRaisesRegex(RuntimeError, 'declined'):
                    setup.accept_terms(base / 'state')
            self.assertFalse((base / 'state').exists())

    def test_remove_controls_keeps_unrelated_bindings_and_user_files(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory)
            hypr = config / 'hypr'
            hypr.mkdir()
            text = 'require("hypr.other")\n' + setup.MARKER + '\n-- personal note\n'
            (hypr / 'bindings.lua').write_text(text)
            (hypr / 'xr-controls.lua').write_text('personal changes')
            setup.remove_controls(config)
            self.assertEqual((hypr / 'bindings.lua').read_text(), 'require("hypr.other")\n-- personal note\n')
            self.assertEqual(next(hypr.glob('bindings.lua.before-xr-remove-*')).read_text(), text)
            self.assertEqual((hypr / 'xr-controls.lua').read_text(), 'personal changes')

    @unittest.skipUnless(os.environ.get('XR_PACKAGED_SDK'), 'Opt-in release runtime load test')
    def test_packaged_vendor_binary_loads_and_reports_version(self):
        library = ctypes.CDLL(os.environ['XR_PACKAGED_SDK'])
        library.GetVersionString.restype = ctypes.c_char_p
        self.assertEqual(library.GetVersionString(), b'2.4.0')


class UserSetupTests(unittest.TestCase):
    def test_setup_preserves_marketplace_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            target = base / 'omarchy/plugins' / setup.PLUGIN_ID
            target.mkdir(parents=True)
            (target / '.git').mkdir()
            (target / 'manifest.json').write_text(json.dumps({'id': setup.PLUGIN_ID}))
            custom = target / 'custom.qml'
            custom.write_text('personal customization')
            with patch.object(setup, 'module') as installer, patch('subprocess.run'), patch('subprocess.check_output', return_value=json.dumps([{'id': setup.PLUGIN_ID}])):
                setup.setup(base)
            installer.return_value.install_plugin_files.assert_not_called()
            self.assertEqual(custom.read_text(), 'personal customization')

    def test_setup_refuses_unrelated_plugin_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            target = base / 'omarchy/plugins' / setup.PLUGIN_ID
            target.mkdir(parents=True)
            (target / 'manifest.json').write_text(json.dumps({'id': 'other.plugin'}))
            with patch.object(setup, 'module'), patch('subprocess.run') as run:
                with self.assertRaisesRegex(RuntimeError, 'unrelated'):
                    setup.setup(base)
            run.assert_not_called()
            self.assertEqual(json.loads((target / 'manifest.json').read_text())['id'], 'other.plugin')

    def test_packaged_sdk_cannot_open_before_acceptance(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            library = base / 'libglasses.so'
            library.write_bytes(b'placeholder')
            instance = SDK(base)
            with patch('sdk.PACKAGED_LIBRARY', library), patch('sdk.PACKAGED_TERMS', base / 'missing-terms'), patch.dict(os.environ, {}, clear=True), patch.object(instance, 'devices') as devices:
                with self.assertRaisesRegex(RuntimeError, 'accept'):
                    instance.connect()
            devices.assert_not_called()
