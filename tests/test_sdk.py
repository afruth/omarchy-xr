import ctypes as C
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "studio"))
from sdk_worker import Session
from sdk import SDK


class SessionTests(unittest.TestCase):
    def make_session(self):
        calls = []
        library = Mock()
        def function(name, result):
            def run(*args):
                calls.append(name)
                return result
            setattr(library, "xr_device_provider_" + name, Mock(side_effect=run))
        for name in ("initialize", "start", "stop", "shutdown", "destroy",
                     "register_imu_pose_callback", "open_imu", "close_imu", "set_display_mode"):
            function(name, 0)
        function("create", 0x100000001)  # ensure pointer-sized handles, not c_int
        function("get_device_type", 1)
        function("is_product_id_valid", 1)
        function("get_display_mode", 0x31)
        function("get_brightness_level", 4)
        with patch("sdk_worker.C.CDLL", return_value=library):
            session = Session("/fake/libglasses.so")
        return session, library, calls

    def test_connect_tracking_expiry_and_cleanup(self):
        session, library, calls = self.make_session()
        session.connect(0x1301)
        self.assertTrue(session.state()["communication"])
        self.assertFalse(session.state()["tracking"])
        session.callback((C.c_float * 7)(0, 0, 0, 1, 0, 0, 0), 1)
        self.assertTrue(session.state()["tracking"])
        session.last_pose -= 3
        self.assertFalse(session.state()["tracking"])
        session.close()
        self.assertEqual(calls[-4:], ["close_imu", "stop", "shutdown", "destroy"])
        self.assertFalse(session.state()["communication"])

    def test_start_failure_cleans_initialized_handle(self):
        session, library, calls = self.make_session()
        library.xr_device_provider_start.side_effect = lambda *args: -2
        with self.assertRaisesRegex(RuntimeError, "USB inaccessible"):
            session.connect(0x1301)
        self.assertEqual(calls[-2:], ["shutdown", "destroy"])
        self.assertIsNone(session.handle)

    def test_tracking_failure_keeps_device_controls(self):
        session, library, calls = self.make_session()
        library.xr_device_provider_open_imu.side_effect = lambda *args: -4
        session.connect(0x1301)
        self.assertTrue(session.communication)
        self.assertIn("Unsupported", session.tracking_error)
        session.close()

    def test_display_query_failure_does_not_disable_tracking(self):
        session, library, calls = self.make_session()
        library.xr_device_provider_get_display_mode.side_effect = lambda *args: -7
        session.connect(0x1301)
        self.assertTrue(session.communication)
        self.assertTrue(session.imu)
        self.assertIsNone(session.mode)
        self.assertIn("rejected", session.state()["displayError"])
        session.close()

    def test_reapply_readback_mismatch(self):
        session, library, calls = self.make_session()
        session.connect(0x1301)
        library.xr_device_provider_get_display_mode.side_effect = [0x31, 0x32]
        with self.assertRaisesRegex(RuntimeError, "readback"):
            session.restore_display()
        library.xr_device_provider_set_display_mode.assert_called_once_with(session.handle, 0x31)
        session.close()


class SupervisorTests(unittest.TestCase):
    def test_missing_sdk_does_not_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = SDK(directory)
            with patch.object(sdk, "library", return_value=Path(directory)/"absent"), patch("sdk.subprocess.Popen") as spawn:
                with self.assertRaisesRegex(RuntimeError, "SDK missing"):
                    sdk.connect()
                spawn.assert_not_called()

    def test_timeout_and_unplug_clear_connected_state(self):
        for unplug in (False, True):
            with self.subTest(unplug=unplug), tempfile.TemporaryDirectory() as directory:
                sdk = SDK(directory)
                process = Mock()
                process.poll.return_value = None
                sdk.process = process
                sdk.pid = 0x1301
                sdk.started = time.time() - 30
                sdk.state = {"communication": True, "tracking": True}
                with patch.object(sdk, "devices", return_value=[] if unplug else [0x1301]):
                    state = sdk.status()
                self.assertFalse(state["communication"])
                self.assertFalse(state["tracking"])
                self.assertTrue(state["error"])
                self.assertIn("unplugged" if unplug else "timed out", state["message"])
                process.communicate.assert_called_once()

    def test_worker_result_finishes_pending_request(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = SDK(directory)
            sdk.process = Mock()
            sdk.process.poll.return_value = None
            sdk.pid = 0x1301
            sdk.started = time.time()
            sdk.pending = True
            sdk.state_file.write_text(json.dumps({"heartbeat": time.time(), "sequence": 1,
                "communication": True, "tracking": False, "message": "Connected"}))
            with patch.object(sdk, "devices", return_value=[0x1301]):
                self.assertFalse(sdk.status()["busy"])
            sdk.disconnect()
