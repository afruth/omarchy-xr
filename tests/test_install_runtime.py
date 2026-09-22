import hashlib
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from studio import install_runtime


class RuntimeInstallTests(unittest.TestCase):
    def test_release_pin_matches_published_package(self):
        self.assertEqual(install_runtime.PACKAGE, 'omarchy-xr-bin-0.3.1-1-x86_64.pkg.tar.zst')
        self.assertEqual(install_runtime.URL,
                         'https://github.com/afruth/omarchy-xr/releases/download/v0.3.1/'
                         + install_runtime.PACKAGE)
        self.assertEqual(install_runtime.PACKAGE_SIZE, 2_933_400)
        self.assertEqual(install_runtime.SHA256,
                         'e1e7f5eae86b63b9ec3fd8e3c4b697bf99e1110a5506d12ecbd08915bb49816f')

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
                patch.object(install_runtime, 'PACKAGE_SIZE', len(payload)), \
                patch.object(install_runtime, 'SHA256', hashlib.sha256(payload).hexdigest()), \
                patch('subprocess.run', side_effect=run) as runner:
            install_runtime.install(controls=True, notifications=True)
        self.assertEqual(runner.call_count, 2)
        self.assertFalse(packages[0].exists())

    def test_checksum_mismatch_never_invokes_package_manager(self):
        with patch('urllib.request.urlopen', return_value=io.BytesIO(b'corrupted download')), \
                patch.object(install_runtime, 'PACKAGE_SIZE', len(b'corrupted download')), \
                patch('subprocess.run') as runner:
            with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
                install_runtime.install()
        runner.assert_not_called()

    def test_failed_package_install_does_not_enable_plugin(self):
        payload = b'complete application package'
        with patch('urllib.request.urlopen', return_value=io.BytesIO(payload)), \
                patch.object(install_runtime, 'PACKAGE_SIZE', len(payload)), \
                patch.object(install_runtime, 'SHA256', hashlib.sha256(payload).hexdigest()), \
                patch('subprocess.run', side_effect=subprocess.CalledProcessError(1, 'pacman')) as runner:
            with self.assertRaises(subprocess.CalledProcessError):
                install_runtime.install()
        self.assertEqual(runner.call_count, 1)

    def test_unbounded_response_stops_at_one_byte_over_limit(self):
        class EndlessResponse(io.BytesIO):
            consumed = 0

            def read(self, size=-1):
                self.consumed += size
                return b'x' * size

        response = EndlessResponse()
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / install_runtime.PACKAGE
            with patch('urllib.request.urlopen', return_value=response):
                with self.assertRaisesRegex(RuntimeError, 'exceeds expected size'):
                    install_runtime.download(destination)
            self.assertFalse(destination.exists())
        self.assertEqual(response.consumed, install_runtime.PACKAGE_SIZE + 1)
        self.assertTrue(response.closed)

    def test_invalid_size_never_invokes_package_manager(self):
        for payload in (b'', b'short', b'oversized'):
            with self.subTest(payload=payload), \
                    patch('urllib.request.urlopen', return_value=io.BytesIO(payload)), \
                    patch.object(install_runtime, 'PACKAGE_SIZE', 6), \
                    patch('subprocess.run') as runner:
                with self.assertRaisesRegex(RuntimeError, 'size'):
                    install_runtime.install()
                runner.assert_not_called()

    def test_failed_download_discards_partial_or_corrupt_file(self):
        class InterruptedResponse(io.BytesIO):
            def read(self, size=-1):
                if self.tell():
                    raise OSError('Transfer interrupted')
                return super().read(3)

        for response, error in ((io.BytesIO(b'short'), RuntimeError),
                                (io.BytesIO(b'corrupt'), RuntimeError),
                                (InterruptedResponse(b'partial'), OSError)):
            with self.subTest(response=response), tempfile.TemporaryDirectory() as directory:
                destination = Path(directory) / install_runtime.PACKAGE
                with patch('urllib.request.urlopen', return_value=response), \
                        patch.object(install_runtime, 'PACKAGE_SIZE', 7):
                    with self.assertRaises(error):
                        install_runtime.download(destination)
                self.assertFalse(destination.exists())
                self.assertTrue(response.closed)

    def test_short_reads_do_not_end_a_valid_transfer(self):
        class ShortResponse(io.BytesIO):
            def read(self, size=-1):
                return super().read(min(size, 3))

        payload = b'complete application package'
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / install_runtime.PACKAGE
            with patch('urllib.request.urlopen', return_value=ShortResponse(payload)), \
                    patch.object(install_runtime, 'PACKAGE_SIZE', len(payload)), \
                    patch.object(install_runtime, 'SHA256', hashlib.sha256(payload).hexdigest()):
                install_runtime.download(destination)
            self.assertEqual(destination.read_bytes(), payload)
