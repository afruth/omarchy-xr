import json
from pathlib import Path
import signal
import subprocess
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

    @patch("glasses.os.kill")
    @patch("glasses.worker_pid", return_value=None)
    @patch("glasses.shutil.which", return_value="/usr/bin/pkexec")
    @patch("glasses.controllers", return_value=["USBC000:00"])
    @patch("glasses.subprocess.Popen")
    def test_recovery_timeout_signals_shell_after_work_starts(self, popen, candidates, which, worker, kill):
        process = Mock()
        process.pid = 10
        process.poll.return_value = None
        process.wait.side_effect = subprocess.TimeoutExpired(["pkexec"], 2)
        popen.return_value = process
        clock = {"now": 1000.0}
        recovery = Recovery()
        with patch("glasses.time.monotonic", side_effect=lambda: clock["now"]):
            recovery.start()
            clock["now"] = 1300
            self.assertTrue(recovery.status()["recovering"])
            kill.assert_not_called()
            worker.return_value = 4242
            recovery.status()
            clock["now"] = 1419
            self.assertTrue(recovery.status()["recovering"])
            kill.assert_not_called()
            clock["now"] = 1421
            waiting = recovery.status()
            self.assertTrue(waiting["recovering"])
            self.assertIs(recovery.process, process)
            kill.assert_called_once_with(4242, signal.SIGTERM)
            process.poll.return_value = 143
            finished = recovery.status()
        self.assertFalse(finished["recovering"])
        self.assertIsNone(recovery.process)
        self.assertIn("timed out", finished["recoveryMessage"])
        self.assertNotIn(signal.SIGKILL, [call.args[1] for call in kill.call_args_list])

    @patch("glasses.subprocess.Popen")
    def test_ambiguous_or_missing_controller_does_not_reset(self, popen):
        for devices in ([], ["USBC000:00", "USBC001:00"]):
            with patch("glasses.controllers", return_value=devices):
                with self.assertRaises(RuntimeError):
                    Recovery().start()
        popen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
