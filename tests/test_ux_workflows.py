from pathlib import Path
import socket
import sys
import tempfile
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "studio"))
from backend import Manager, default_layout, perform
from canvas import validate
from test_canvas import CanvasHypr, client


class UXWorkflows(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.fake = CanvasHypr()
        self.manager = Manager(self.temp.name, "/unused", self.fake)
        self.manager.graphics_limits = {"maxWidth": 8192, "maxHeight": 8192}
        self.addCleanup(self.manager.lock.close)
        self.addCleanup(self.manager.cleanup)

    def test_preview_prepares_fresh_monitor_session(self):
        self.manager.start = Mock()
        result = perform(self.manager, {"action": "start", "layout": default_layout()})
        self.assertEqual(len(self.manager.owned), 3)
        self.assertEqual(result["layout"], self.manager.applied)
        self.manager.start.assert_called_once_with()

    def test_preview_uses_pending_canvas_adoption_choice(self):
        self.manager.render_mode = "canvas"
        self.fake.clients = [client("0x1", 1)]
        self.manager.start = Mock()
        result = perform(self.manager, {"action": "start", "canvas": {"adoptPolicy": "empty"}})
        self.assertTrue(self.manager.canvas.active)
        self.assertEqual(result["canvas"]["adoptPolicy"], "empty")
        self.assertEqual(self.fake.clients[0]["workspace"]["id"], 1)

    def test_save_does_not_report_unapplied_draft_as_live(self):
        original = default_layout()
        self.manager.apply(original)
        draft = {**original, "fps": 30}
        result = perform(self.manager, {"action": "save", "layout": draft})
        self.assertTrue(result["layoutDirty"])
        self.assertEqual(self.manager.applied["fps"], 60)
        self.assertEqual(self.manager.load()["fps"], 30)

    def test_save_current_setup_before_switching(self):
        draft = default_layout()
        draft["fps"] = 30
        perform(self.manager, {"action": "use_setup", "setupId": "builtin:focus", "layout": draft,
                               "saveBeforeSwitch": True, "saveSetupName": "My working setup"})
        saved = self.manager.setups()["items"][0]
        self.assertEqual(saved["name"], "My working setup")
        self.assertEqual(saved["layout"]["fps"], 30)
        self.assertEqual(len(self.manager.load()["monitors"]), 1)

    def test_return_window_restores_origin_and_removes_journal_entry(self):
        self.fake.clients = [client("0x1", 2, floating=True)]
        self.manager.canvas.ensure(self.manager.monitors())
        self.manager.canvas.release_window("0x1")
        self.assertEqual(self.fake.clients[0]["workspace"]["id"], 2)
        self.assertEqual(self.fake.clients[0]["size"], [800, 600])
        self.assertNotIn("0x1", self.manager.canvas.read_journal()["windows"])

    def test_failed_return_preserves_recovery_journal(self):
        self.fake.clients = [client("0x1", 1)]
        self.manager.canvas.ensure(self.manager.monitors())
        self.fake.fail = True
        with self.assertRaises(RuntimeError):
            self.manager.canvas.release_window("0x1")
        self.fake.fail = False
        self.assertIn("0x1", self.manager.canvas.read_journal()["windows"])

    def test_address_commands_and_invalid_input(self):
        self.manager.render_mode = "canvas"
        self.manager.viewer = Mock()
        self.manager.viewer.poll.return_value = None
        self.addCleanup(setattr, self.manager, "viewer", None)
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as server:
            server.bind(str(self.manager.pose_socket))
            server.settimeout(1)
            for verb in ("focus", "summon", "pin"):
                perform(self.manager, {"action": "canvas_window", "windowAction": verb, "windowAddress": "0x123"})
                self.assertEqual(server.recv(128), f"{verb}:0x123".encode())
            with self.assertRaises(ValueError):
                perform(self.manager, {"action": "canvas_window", "windowAction": "focus", "windowAddress": "0x123\nstop"})

    def test_interaction_preferences_validate_and_migrate(self):
        settings = validate({})
        self.assertTrue(settings["confirmHints"])
        self.assertFalse(settings["confirmPointerTransfer"])
        for key in ("confirmHints", "confirmPointerTransfer", "followNewWindows"):
            with self.assertRaises(ValueError):
                validate({key: 1})

    def test_comfort_save_waits_for_renderer_and_uses_persistent_directory(self):
        self.manager.pose_socket = Path(self.temp.name) / "runtime" / "pose.sock"
        profile = self.manager.directory / "comfort-view.monitors.tsv"
        def acknowledge(action):
            if action == "save_comfort":
                profile.write_text("v1 5 0 0 0 1 0 0 0 5 0 0\n")
        self.manager.camera_control = Mock(side_effect=acknowledge)
        result = perform(self.manager, {"action": "save_comfort"})
        self.assertIn("saved", result["message"])
        self.assertTrue(profile.exists())
        self.assertEqual(self.manager.camera_control.call_args_list[-1].args, ("comfort_sample_off",))


if __name__ == "__main__":
    unittest.main()
