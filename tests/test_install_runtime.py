import hashlib
import io
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

from studio import install_runtime


class RuntimeInstallTests(unittest.TestCase):
    def test_verified_package_installs_before_user_setup(self):
        payload = b'complete application package'
        packages = []
        def run(command, check):
            if command[0] == 'sudo':
                package = Path(command[-1])
                self.assertEqual(package.read_bytes(), payload)
                self.assertEqual(command[:4], ['sudo', 'pacman', '-U', '--needed'])
                packages.append(package)
            else:
                self.assertEqual(command, ['omarchy-xr-setup', '--controls', '--notifications'])
                self.assertEqual(len(packages), 1)
        with patch('urllib.request.urlopen', return_value=io.BytesIO(payload)), \
                patch.object(install_runtime, 'SHA256', hashlib.sha256(payload).hexdigest()), \
                patch('subprocess.run', side_effect=run) as runner:
            install_runtime.install(controls=True, notifications=True)
        self.assertEqual(runner.call_count, 2)
        self.assertFalse(packages[0].exists())

    def test_checksum_mismatch_never_invokes_package_manager(self):
        with patch('urllib.request.urlopen', return_value=io.BytesIO(b'corrupted download')), \
                patch('subprocess.run') as runner:
            with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
                install_runtime.install()
        runner.assert_not_called()

    def test_failed_package_install_does_not_enable_plugin(self):
        payload = b'complete application package'
        with patch('urllib.request.urlopen', return_value=io.BytesIO(payload)), \
                patch.object(install_runtime, 'SHA256', hashlib.sha256(payload).hexdigest()), \
                patch('subprocess.run', side_effect=subprocess.CalledProcessError(1, 'pacman')) as runner:
            with self.assertRaises(subprocess.CalledProcessError):
                install_runtime.install()
        self.assertEqual(runner.call_count, 1)
