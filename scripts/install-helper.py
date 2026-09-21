#!/usr/bin/python3 -I
"""Install/remove the audited system helper. Requires one administrator approval.

Runtime policy grants only the fixed executable, never a Python interpreter or
user-owned code. Install again explicitly after changing the helper source.
"""
from pathlib import Path
import argparse
import fcntl
import os
import stat
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HELPER = Path('/usr/local/libexec/omarchy-xr-display')
POLICY = Path('/usr/share/polkit-1/actions/io.github.afruth.omarchy-xr.display.policy')


def trusted_directory(path):
    for parent in reversed((path, *path.parents)):
        if not parent.exists():
            parent.mkdir(mode=0o755)
        info = parent.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise RuntimeError(f'Installation requires a root-owned, non-writable directory: {parent}')


def install_file(target, data, mode):
    trusted_directory(target.parent)
    fd, name = tempfile.mkstemp(prefix='.'+target.name+'-', dir=target.parent)
    try:
        with os.fdopen(fd, 'wb') as file:
            file.write(data)
            os.fchmod(file.fileno(), mode)
            os.fchown(file.fileno(), 0, 0)
        os.replace(name, target)
    finally:
        Path(name).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--uninstall', action='store_true')
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error('Run make install-helper (or make uninstall-helper) from a terminal')
    # Never replace/remove a helper while it is managing a display.
    with open('/run/omarchy-xr-display.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if args.uninstall:
            for target in (POLICY, HELPER):
                trusted_directory(target.parent)
                target.unlink(missing_ok=True)
            print('Stereo helper and authorization policy removed.')
        else:
            helper = (ROOT/'studio/dedicated_helper.py').read_bytes()
            compile(helper, str(HELPER), 'exec')
            install_file(HELPER, helper, 0o755)
            install_file(POLICY, (ROOT/'packaging'/POLICY.name).read_bytes(), 0o644)
            print('Stereo helper installed. Active local desktop users can start XR without a password.')


if __name__ == '__main__':
    main()
