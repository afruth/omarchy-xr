"""Install the pinned, complete application package from the public release."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import urllib.request

PACKAGE = 'omarchy-xr-bin-0.3.0-1-x86_64.pkg.tar.zst'
URL = 'https://github.com/afruth/omarchy-xr/releases/download/v0.3.0/' + PACKAGE
SHA256 = '9960d7397fb1d846e5e5c05112c7db3720d05b07a179310f5c6aad2f011630d1'


def download(destination):
    with urllib.request.urlopen(URL, timeout=60) as response, destination.open('wb') as output:
        shutil.copyfileobj(response, output)
    with destination.open('rb') as package:
        actual = hashlib.file_digest(package, 'sha256').hexdigest()
    if actual != SHA256:
        raise RuntimeError('Package checksum mismatch; installation stopped')


def install(controls=False, notifications=False):
    print('Downloading Omarchy XR 0.3.0 with its bundled glasses runtime.', flush=True)
    with tempfile.TemporaryDirectory(prefix='omarchy-xr-install-') as directory:
        package = Path(directory) / PACKAGE
        download(package)
        print('SHA-256 verified. Pacman will ask before installing the package.', flush=True)
        subprocess.run(['sudo', 'pacman', '-U', '--needed', str(package)], check=True)
    command = ['omarchy-xr-setup']
    if controls:
        command.append('--controls')
    if notifications:
        command.append('--notifications')
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--controls', action='store_true')
    parser.add_argument('--notifications', action='store_true')
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error('Run as your desktop user; pacman will request privileges when needed')
    if platform.machine() != 'x86_64':
        parser.error('This release requires Arch Linux x86_64')
    try:
        install(args.controls, args.notifications)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print('Installation failed:', error, flush=True)
        if os.isatty(0):
            input('Press Enter to close this terminal.')
        parser.exit(1)


if __name__ == '__main__':
    main()
