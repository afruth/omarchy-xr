#!/usr/bin/env python3
"""Enable the packaged Omarchy XR UI for the current user, or remove its integration."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

SHARE = Path('/usr/share/omarchy-xr')
PLUGIN_ID = 'afruth.omarchy-xr'
NOTIFICATION_ID = 'afruth.omarchy-xr-notifications'
RENDERER = Path('/usr/bin/omarchy-xr')
MARKER = 'require("hypr.xr-controls")'


def module(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), SHARE / 'scripts' / (name + '.py'))
    assert spec is not None and spec.loader is not None
    imported = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(imported)
    return imported


def accept_terms(state, assume=False):
    terms = (SHARE / 'LICENSE').read_bytes()
    digest = hashlib.sha256(terms).hexdigest()
    record = state / 'license-acceptance.json'
    try:
        if json.loads(record.read_text()).get('sha256') == digest:
            return
    except (OSError, ValueError, AttributeError):
        pass
    if not assume:
        if not sys.stdin.isatty():
            raise RuntimeError('Read /usr/share/omarchy-xr/LICENSE and PRIVACY.md, then rerun with --accept-license')
        print(terms.decode())
        print((SHARE / 'PRIVACY.md').read_text())
        if input('Accept these terms and enable Omarchy XR? [y/N] ').strip().lower() not in ('y', 'yes'):
            raise RuntimeError('Terms declined; nothing was enabled')
    state.mkdir(parents=True, exist_ok=True)
    record.write_text(json.dumps({'sha256': digest, 'acceptedAt': int(time.time())}) + '\n')
    record.chmod(0o600)


def remove_controls(config):
    bindings = config / 'hypr/bindings.lua'
    if not bindings.is_file():
        return
    text = bindings.read_text()
    lines = text.splitlines(keepends=True)
    # Remove only the exact hook installed by install-controls.py. Keep personal files.
    filtered = ''.join(line for line in lines if line.strip() != MARKER)
    if filtered != text:
        shutil.copy2(bindings, bindings.with_name('bindings.lua.before-xr-remove-' + str(time.time_ns())))
        bindings.write_text(filtered)


def remove(config):
    for identity in (NOTIFICATION_ID, PLUGIN_ID):
        if (config / 'omarchy/plugins' / identity / 'manifest.json').is_file():
            subprocess.run(['omarchy', 'plugin', 'remove', identity, '--yes'], check=True)
    remove_controls(config)
    subprocess.run(['hyprctl', 'reload'], check=True)
    subprocess.run(['hyprctl', 'configerrors'], check=True)
    print('XR integration removed. Layouts, images and personal control files were retained.')


def setup(config, controls=False, notifications=False):
    installer = module('install-studio')
    target = config / 'omarchy/plugins' / PLUGIN_ID
    # Preserve marketplace checkouts, their git history, and user edits.
    if target.is_symlink():
        raise RuntimeError('Resolve the existing plugin symlink before installing the package UI')
    if target.exists():
        manifest = json.loads((target / 'manifest.json').read_text())
        if manifest.get('id') != PLUGIN_ID:
            raise RuntimeError('Refusing to replace an unrelated plugin')
    if not (target / '.git').exists():
        if target.exists():
            backup = target.with_name('.' + PLUGIN_ID + '.before-package-' + str(time.time_ns()))
            shutil.copytree(target, backup, symlinks=True)
        installer.install_plugin_files(SHARE / 'plugin', target, RENDERER)
    subprocess.run(['omarchy-shell', 'shell', 'rescanPlugins'], check=True)
    for _ in range(40):
        listing = subprocess.check_output(['omarchy', 'plugin', 'list', '--json'], text=True)
        if any(item.get('id') == PLUGIN_ID for item in json.loads(listing)):
            break
        time.sleep(.1)
    subprocess.run(['omarchy', 'plugin', 'enable', PLUGIN_ID], check=True)
    subprocess.run(['omarchy', 'bar', 'put', PLUGIN_ID], check=True)
    if controls:
        env = {**os.environ, 'PYTHONPATH': str(SHARE / 'plugin/studio')}
        subprocess.run([sys.executable, str(SHARE / 'scripts/install-controls.py')], env=env, check=True)
        subprocess.run(['hyprctl', 'reload'], check=True)
        subprocess.run(['hyprctl', 'configerrors'], check=True)
    if notifications:
        subprocess.run([sys.executable, str(SHARE / 'scripts/install-notifications.py')], check=True)
    subprocess.run(['omarchy-shell', 'shell', 'summon', PLUGIN_ID, '{}'], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--accept-license', action='store_true', help='Accept the installed application and SDK terms')
    parser.add_argument('--controls', action='store_true', help='Install the backed-up Hyprland controls integration')
    parser.add_argument('--notifications', action='store_true', help='Enable XR mirroring of native notifications')
    parser.add_argument('--remove', action='store_true', help='Remove shell integration; stop XR in Studio first')
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error('Run setup as your desktop user, not root')
    config = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config')))
    state = Path(os.environ.get('XDG_STATE_HOME', str(Path.home() / '.local/state'))) / 'omarchy-xr'
    try:
        if args.remove:
            remove(config)
        else:
            accept_terms(state, args.accept_license)
            setup(config, args.controls, args.notifications)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
