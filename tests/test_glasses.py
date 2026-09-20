import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "studio"))
from glasses import Recovery, controllers, detect, RESET_SCRIPT


class GlassesTests(unittest.TestCase):
    def test_usb_and_video_are_independent(self):
        with tempfile.TemporaryDirectory() as directory:
            usb = Path(directory)
            device = usb / "3-6"
            device.mkdir()
            (device / "idVendor").write_text("35ca\n")
            (device / "product").write_text("VITURE PRO 2 XR Glasses")
            result = detect([{"name": "DP-1", "description": "Unrelated monitor", "width": 1920}], usb)
            self.assertTrue(result["usb"])
            self.assertEqual(result["displays"], [])
            (device / "idVendor").unlink()
            result = detect([{"name": "DP-2", "model": "VITURE", "width": 1920}], usb)
            self.assertFalse(result["usb"])
            self.assertEqual(result["displays"], ["DP-2"])
            self.assertEqual(detect([{"name": "DP-2", "model": "VITURE", "width": 1920, "disabled": True}], usb)["displays"], [])

    def test_controller_allowlist(self):
        with tempfile.TemporaryDirectory() as directory:
            driver = Path(directory)
            (driver / "USBC000:00").symlink_to(driver)
            (driver / "USBC001:00").mkdir()
            (driver / "USBC-unsafe").symlink_to(driver)
            self.assertEqual(controllers(driver), ["USBC000:00"])

    @patch("glasses.shutil.which", return_value="/usr/bin/pkexec")
    @patch("glasses.controllers", return_value=["USBC000:00"])
    @patch("glasses.subprocess.Popen")
    def test_async_recovery_and_retry(self, popen, candidates, which):
        process = Mock()
        process.poll.return_value = None
        popen.return_value = process
        recovery = Recovery()
        recovery.start()
        command = popen.call_args.args[0]
        self.assertEqual(command, ["pkexec", "/bin/sh", "-c", RESET_SCRIPT, "omarchy-xr-reset", "USBC000:00"])
        self.assertTrue(recovery.status()["recovering"])
        with self.assertRaises(RuntimeError):
            recovery.start()
        process.poll.return_value = 126
        self.assertIn("cancelled", recovery.status()["recoveryMessage"])
        recovery.start()
        process.poll.return_value = 0
        state = recovery.status()
        self.assertFalse(state["recovering"])
        self.assertIn("Waiting for video", state["recoveryMessage"])
        self.assertTrue(state["canReset"])

    @patch("glasses.subprocess.Popen")
    def test_ambiguous_or_missing_controller_does_not_reset(self, popen):
        for devices in ([], ["USBC000:00", "USBC001:00"]):
            with patch("glasses.controllers", return_value=devices):
                with self.assertRaises(RuntimeError):
                    Recovery().start()
        popen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
