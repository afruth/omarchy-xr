import ctypes as C
import json
from pathlib import Path
import sys
import tempfile
import errno
import socket
import time
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "studio"))
from sdk_worker import Session, PosePublisher, record_keep_alive
from clock import boot_time
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
        function("is_product_support_native_dof", 0)
        function("get_display_mode", 0x31)
        function("get_brightness_level", 4)
        with patch("sdk_worker.C.CDLL", return_value=library):
            session = Session("/fake/libglasses.so")
        return session, library, calls

    def test_connect_tracking_expiry_and_cleanup(self):
        session, library, calls = self.make_session()
        session.connect(0x1301)
        self.assertTrue(session.state()["communication"])
        self.assertFalse(session.state()["nativeDof"])
        self.assertFalse(session.state()["tracking"])
        session.callback((C.c_float * 7)(0, 0, 0, 1, 0, 0, 0), 1)
        self.assertTrue(session.state()["tracking"])
        session.last_pose -= 3
        self.assertFalse(session.state()["tracking"])
        session.close()
        self.assertEqual(calls[-4:], ["close_imu", "stop", "shutdown", "destroy"])
        self.assertFalse(session.state()["communication"])

    def test_pose_packet_validation_and_expiry(self):
        session, _, _ = self.make_session()
        self.assertIsNone(session.pose_packet())
        session.on_pose((C.c_float * 7)(float("nan"), 0, 0, 0, 0, 0, 0), 1)
        self.assertEqual(session.samples, 0)
        session.on_pose((C.c_float * 7)(10, -20, 30, 2, 0, 0, 0), 1)
        packet = session.pose_packet().decode().split()
        self.assertEqual(packet[0], "euler-nwu-v2")
        self.assertEqual([float(v) for v in packet[2:5]], [10, -20, 30])
        self.assertEqual(packet[5], "1")
        with patch("sdk_worker.time.monotonic", return_value=float(packet[1])+1):
            self.assertIsNone(session.pose_packet())
        session.close()
        self.assertIsNone(session.pose_packet())

    def test_keep_alive_survives_two_failures(self):
        self.assertEqual(record_keep_alive(0, "USB transfer failed"), 1)
        self.assertEqual(record_keep_alive(1, "USB transfer failed"), 2)
        with self.assertRaisesRegex(RuntimeError, "USB transfer failed"):
            record_keep_alive(2, "USB transfer failed")

    def test_pose_publisher_delivers_without_control_loop(self):
        session, _, _ = self.make_session()
        with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as receiver:
            path=str(Path(directory)/"pose.sock")
            try:
                receiver.bind(path)
            except PermissionError:
                self.skipTest("Unix sockets restricted by sandbox")
            receiver.settimeout(1)
            publisher=PosePublisher(session,path)
            try:
                session.on_pose((C.c_float * 7)(15,-25,35,1,0,0,0),1)
                packet=receiver.recv(256).decode().split()
                self.assertEqual(packet[0],"euler-nwu-v2")
                self.assertEqual([float(v) for v in packet[2:5]],[15,-25,35])
                self.assertEqual(packet[5],"1")
            finally:publisher.close()
        session.close()

    def test_missing_pose_socket_does_not_busy_loop(self):
        session, _, _ = self.make_session()
        publisher = PosePublisher(session, "/tmp/omarchy-xr-missing-pose.sock")
        attempts = []
        class MissingSocket:
            def sendto(self, packet, path):
                attempts.append(time.monotonic())
                raise OSError(errno.ENOENT, "missing")
            def close(self):
                pass
        real = publisher.socket
        publisher.socket = MissingSocket()
        try:
            session.on_pose((C.c_float * 7)(1, 2, 3, 1, 0, 0, 0), 1)
            time.sleep(0.6)
            self.assertGreaterEqual(len(attempts), 1)
            self.assertLessEqual(len(attempts), 4)
            if len(attempts) >= 2:
                self.assertGreater(min(b - a for a, b in zip(attempts, attempts[1:])), 0.2)
        finally:
            publisher.close()
            real.close()
        self.assertFalse(publisher.thread.is_alive())
        session.close()

    def test_start_failure_cleans_initialized_handle(self):
        session, library, calls = self.make_session()
        library.xr_device_provider_start.side_effect = lambda *args: -2
        with self.assertRaisesRegex(RuntimeError, "could not be accessed over USB"):
            session.connect(0x1301)
        self.assertEqual(calls[-2:], ["shutdown", "destroy"])
        self.assertIsNone(session.handle)

    def test_tracking_failure_keeps_device_controls(self):
        session, library, calls = self.make_session()
        library.xr_device_provider_open_imu.side_effect = lambda *args: -4
        session.connect(0x1301)
        self.assertTrue(session.communication)
        self.assertIn("do not support", session.tracking_error)
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

    def test_stereo_restores_original_mode_and_journal(self):
        session, library, calls = self.make_session()
        with tempfile.TemporaryDirectory() as directory:
            session.mode_journal = Path(directory)/"mode.json"
            session.connect(0x1301)
            library.xr_device_provider_get_display_mode.side_effect = [0x34,0x32,0x34]
            session.begin_stereo()
            self.assertEqual(session.mode,0x32)
            self.assertEqual(json.loads(session.mode_journal.read_text()),{"mode":0x34})
            session.end_stereo()
            session.verify_restore()
            self.assertIsNone(session.original_mode)
            self.assertFalse(session.mode_journal.exists())
            session.close()

    def test_stereo_ack_does_not_require_host_timing_to_change_immediately(self):
        session, library, _ = self.make_session()
        session.connect(0x1301)
        library.xr_device_provider_get_display_mode.side_effect = [0x34,0x34,0x32,0x34]
        session.begin_stereo()
        self.assertEqual(session.mode,0x34)  # host is still driving its old timing
        self.assertEqual(session.original_mode,0x34)
        session.wait_mode(0x32)  # verified after direct scanout starts
        session.end_stereo()
        session.verify_restore()
        self.assertIsNone(session.original_mode)
        session.close()

    def test_reconnect_does_not_switch_already_restored_mode(self):
        session, library, _ = self.make_session()
        with tempfile.TemporaryDirectory() as directory:
            session.mode_journal=Path(directory)/"mode.json"
            session.mode_journal.write_text('{"mode":52}')
            library.xr_device_provider_get_display_mode.side_effect=lambda *args:0x34
            session.connect(0x1301)
            library.xr_device_provider_set_display_mode.assert_not_called()
            self.assertIsNone(session.original_mode)
            self.assertFalse(session.mode_journal.exists())
            session.close()
            library.xr_device_provider_set_display_mode.assert_not_called()

    def test_reconnect_defers_mismatched_mode_to_supervisor(self):
        session, library, _ = self.make_session()
        with tempfile.TemporaryDirectory() as directory:
            session.mode_journal=Path(directory)/"mode.json"
            session.mode_journal.write_text('{"mode":52}')
            library.xr_device_provider_get_display_mode.side_effect=lambda *args:0x32
            session.connect(0x1301)
            library.xr_device_provider_set_display_mode.assert_not_called()
            self.assertEqual(session.original_mode,0x34)
            self.assertTrue(session.mode_journal.exists())
            self.assertIn("pending",session.display_error)
            session.close()
            library.xr_device_provider_set_display_mode.assert_not_called()
            self.assertTrue(session.mode_journal.exists())

    def test_pending_recovery_query_failure_preserves_tracking(self):
        session, library, _ = self.make_session()
        with tempfile.TemporaryDirectory() as directory:
            session.mode_journal=Path(directory)/"mode.json"
            session.mode_journal.write_text('{"mode":52}')
            library.xr_device_provider_get_display_mode.side_effect=lambda *args:-7
            session.connect(0x1301)
            self.assertTrue(session.communication)
            self.assertTrue(session.imu)
            self.assertEqual(session.original_mode,0x34)
            session.close()
            library.xr_device_provider_set_display_mode.assert_not_called()
            self.assertTrue(session.mode_journal.exists())

    def test_reapply_readback_mismatch(self):
        session, library, calls = self.make_session()
        session.connect(0x1301)
        library.xr_device_provider_get_display_mode.side_effect = [0x31, 0x32]
        with self.assertRaisesRegex(RuntimeError, "readback"):
            session.restore_display()
        library.xr_device_provider_set_display_mode.assert_called_once_with(session.handle, 0x31)
        session.close()


class SupervisorTests(unittest.TestCase):
    def test_worker_receives_the_viewer_pose_socket(self):
        with tempfile.TemporaryDirectory() as directory:
            pose = Path(directory) / "runtime" / "pose.sock"
            library = Path(directory) / "lib.so"
            library.write_text("")
            sdk = SDK(directory, pose)
            with patch.object(sdk, "library", return_value=library), patch.object(sdk, "devices", return_value=[0x1301]), patch("sdk.subprocess.Popen") as spawn:
                sdk.connect()
            self.assertEqual(spawn.call_args.args[0][-1], str(pose))
            sdk.process = None

    def test_missing_sdk_does_not_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = SDK(directory)
            with patch.object(sdk, "library", return_value=Path(directory)/"absent"), patch("sdk.subprocess.Popen") as spawn:
                with self.assertRaisesRegex(RuntimeError, "Head tracking software is missing"):
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
                sdk.started = boot_time() - 30
                sdk.state = {"communication": True, "tracking": True}
                with patch.object(sdk, "devices", return_value=[] if unplug else [0x1301]):
                    state = sdk.status()
                self.assertFalse(state["communication"])
                self.assertFalse(state["tracking"])
                self.assertTrue(state["error"])
                self.assertIn("disconnected" if unplug else "stopped responding", state["message"])
                process.communicate.assert_called_once()

    def test_worker_result_finishes_pending_request(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = SDK(directory)
            sdk.process = Mock()
            sdk.process.poll.return_value = None
            sdk.pid = 0x1301
            sdk.started = boot_time()
            sdk.pending = True
            sdk.state_file.write_text(json.dumps({"heartbeat": boot_time(), "sequence": 1,
                "communication": True, "tracking": False, "message": "Connected"}))
            with patch.object(sdk, "devices", return_value=[0x1301]):
                self.assertFalse(sdk.status()["busy"])
            sdk.disconnect()
