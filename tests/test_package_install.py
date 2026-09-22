"""Opt-in installed-path checks using the actual Omarchy CLI in a private namespace.

XR_PACKAGE_ROOT names an extracted package root containing usr/. Only compositor
and shell IPC are doubled. No host config, device, socket or network is writable.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = os.environ.get('XR_PACKAGE_ROOT')
PLUGIN = 'afruth.omarchy-xr'
NOTIFICATIONS = 'afruth.omarchy-xr-notifications'


@unittest.skipUnless(PACKAGE, 'Set XR_PACKAGE_ROOT to test an extracted release package')
class InstalledPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='xr-package-install-')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.home = self.base / 'home'
        self.config = self.home / '.config'
        (self.config / 'hypr').mkdir(parents=True)
        (self.config / 'hypr/hyprland.lua').write_text('-- isolated session\n')
        (self.config / 'hypr/bindings.lua').write_text('-- personal bindings\n')
        self.bin = self.base / 'bin'
        self.bin.mkdir()
        for name in ('omarchy-shell', 'hyprctl'):
            shutil.copyfile(ROOT / 'tests/package_session.py', self.bin / name)
            (self.bin / name).chmod(0o755)

    def command(self, *args, upgrade=None):
        options = ['bwrap', '--die-with-parent', '--unshare-all', '--ro-bind', '/', '/',
                   '--overlay-src', '/usr', '--overlay-src', str(Path(PACKAGE).resolve() / 'usr')]
        if upgrade:
            options += ['--overlay-src', str(upgrade)]
        options += ['--ro-overlay', '/usr', '--tmpfs', '/home', '--tmpfs', '/root',
                    '--tmpfs', '/run', '--tmpfs', '/tmp', '--tmpfs', '/sys', '--dev', '/dev', '--proc', '/proc',
                    '--bind', str(self.home), '/home/package-user', '--ro-bind', str(self.bin), '/tmp/test-bin',
                    '--clearenv', '--setenv', 'HOME', '/home/package-user',
                    '--setenv', 'PATH', '/tmp/test-bin:/usr/share/omarchy/bin:/usr/bin',
                    '--setenv', 'OMARCHY_PATH', '/usr/share/omarchy', '--setenv', 'PYTHONDONTWRITEBYTECODE', '1',
                    '--setenv', 'LC_ALL', 'C.UTF-8', '--chdir', '/home/package-user']
        return subprocess.run(options + list(args), capture_output=True, text=True, timeout=30)

    def successful(self, *args, **kwargs):
        result = self.command(*args, **kwargs)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout.strip()

    def install(self):
        self.successful('omarchy-xr-setup', '--accept-license', '--controls', '--notifications')

    def test_first_use_update_and_remove(self):
        refused = self.command('omarchy-xr-setup', '--controls', '--notifications')
        self.assertNotEqual(refused.returncode, 0)
        self.assertIn('--accept-license', refused.stderr)
        self.assertFalse((self.config / 'omarchy/plugins').exists())
        self.assertFalse((self.home / '.local/state/omarchy-xr/license-acceptance.json').exists())
        self.install()
        plugins = self.config / 'omarchy/plugins'
        for identity in (PLUGIN, NOTIFICATIONS):
            self.successful('omarchy', 'plugin', 'validate', '/home/package-user/.config/omarchy/plugins/' + identity)
        state = json.loads((self.home / 'session.json').read_text())
        self.assertEqual(set(state['enabled']), {PLUGIN, NOTIFICATIONS})
        self.assertEqual(state['bar'], [PLUGIN])
        launcher = '/home/package-user/.config/omarchy/plugins/' + PLUGIN + '/bin/omarchy-xr'
        self.assertEqual(self.successful(launcher, '--version'), 'omarchy-xr 0.3.1')
        saved = self.home / '.local/state/omarchy-xr/layout.json'
        saved.write_text('{"personal": true}')
        self.install()
        self.assertTrue(list(plugins.glob('.' + PLUGIN + '.before-package-*')))
        self.assertEqual((self.config / 'hypr/bindings.lua').read_text().count('require("hypr.xr-controls")'), 1)
        self.successful('omarchy-xr-setup', '--remove')
        self.assertFalse((plugins / PLUGIN).exists())
        self.assertFalse((plugins / NOTIFICATIONS).exists())
        self.assertTrue(list(plugins.glob('.' + NOTIFICATIONS + '.bak.*')))
        self.assertEqual(json.loads((self.home / 'session.json').read_text())['enabled'], [])
        self.assertEqual(json.loads(saved.read_text()), {'personal': True})
        self.assertTrue((self.config / 'hypr/xr-controls.lua').is_file())
        self.assertNotIn('require("hypr.xr-controls")', (self.config / 'hypr/bindings.lua').read_text())

    def test_renderer_upgrade_is_used_without_rerunning_setup(self):
        self.install()
        upgrade = self.base / 'upgrade'
        (upgrade / 'bin').mkdir(parents=True)
        renderer = upgrade / 'bin/omarchy-xr'
        renderer.write_text('#!/bin/sh\nprintf "upgraded renderer\\n"\n')
        renderer.chmod(0o755)
        launcher = '/home/package-user/.config/omarchy/plugins/' + PLUGIN + '/bin/omarchy-xr'
        self.assertEqual(self.successful(launcher, '--version', upgrade=upgrade), 'upgraded renderer')
