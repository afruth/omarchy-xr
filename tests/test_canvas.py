import json
import os
from pathlib import Path
import re
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"studio"))
sys.path.insert(0,str(Path(__file__).resolve().parent))
from backend import Manager, default_layout, perform, validate as validate_layout
from canvas import DEFAULTS, validate
from test_studio import FakeHypr

GOLDEN = "# canvas v1 60 2.4 60 0.35 0.8 1 300 all 1"
NAMED = {"omxr-canvas": -1338, "omxr-park": -1337}


def client(address, workspace, floating=False, **extra):
    if not isinstance(workspace, dict):
        workspace = {"id": NAMED.get(workspace, workspace), "name": str(workspace)}
    return {"address": address, "mapped": True, "hidden": False, "workspace": workspace,
            "floating": floating, "size": [800, 600], "at": [100, 200], "pid": 4242, "class": "foot",
            "title": "shell", **extra}


class CanvasHypr(FakeHypr):
    """FakeHypr plus clients, window dispatches and outputs created with their hl.monitor geometry."""
    def __init__(self):
        super().__init__()
        self.outputs["eDP-1"].update(refreshRate=60, focused=True, activeWorkspace={"id": 1, "name": "1"})
        self.clients = []
        self.calls = []
        self.rules = {}
        self.on_move = None

    def __call__(self, *args):
        self.calls.append(args)
        if args == ("-j", "clients"): return json.dumps(self.clients)
        if args[:2] == ("output", "create"):
            self.outputs[args[3]] = {"name": args[3], **self.rules[args[3]], "activeWorkspace": {"id": -1338, "name": "omxr-canvas"}}
            return "ok"
        if args[0] == "eval": return self.eval(args[1])
        return super().__call__(*args)

    def evals(self, text=""):
        return [a[1] for a in self.calls if a[0] == "eval" and text in a[1]]

    def window(self, address):
        return next(c for c in self.clients if c["address"] == address)

    def eval(self, code):
        if self.fail: raise RuntimeError("Injected compositor failure")
        rule = re.search(r'hl.monitor\(\{output="([^"]+)", mode="(\d+)x(\d+)@(\d+)", position="(-?\d+)x(-?\d+)", scale=([0-9.]+)', code)
        if rule:
            name, w, h, rate, x, y, scale = rule.groups()
            self.rules[name] = dict(width=int(w), height=int(h), refreshRate=int(rate), x=int(x), y=int(y), scale=float(scale))
            if name in self.outputs: self.outputs[name].update(self.rules[name])
            return "ok"
        if "hl.dsp.workspace.move" in code: self.move_workspace(code)
        self.dispatch_windows(code)
        return "ok"

    def dispatch_windows(self, code):
        for address, target in re.findall(r'window.move\(\{window="address:(0x[0-9a-f]+)", workspace="([^"]+)", follow=false', code):
            if self.on_move: self.on_move()
            name = target.removeprefix("name:")
            self.window(address)["workspace"] = {"id": NAMED.get(name) or int(name), "name": name}
        for address, action in re.findall(r'window.float\(\{window="address:(0x[0-9a-f]+)", action="(\w+)"', code):
            self.window(address)["floating"] = action == "float"
        for address, w, h in re.findall(r'window.resize\(\{window="address:(0x[0-9a-f]+)", x=(\d+), y=(\d+)', code):
            self.window(address)["size"] = [int(w), int(h)]
        for address, x, y in re.findall(r'window.move\(\{window="address:(0x[0-9a-f]+)", x=(-?\d+), y=(-?\d+)', code):
            self.window(address)["at"] = [int(x), int(y)]


class CanvasTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.runtime = Path(self.temp.name) / "runtime"
        self.runtime.mkdir()
        (self.runtime / "controls.version").write_text("6\n")
        self.state = Path(self.temp.name) / "state"
        env = patch.dict(os.environ, {"OMARCHY_XR_RUNTIME": str(self.runtime), "HYPRLAND_INSTANCE_SIGNATURE": "test-session"})
        env.start()
        self.addCleanup(env.stop)
        self.addCleanup(self.temp.cleanup)

    def manager(self, fake, mode=None):
        manager = Manager(self.state, "/unused", fake)
        manager.graphics_limits = {"maxWidth": 8192, "maxHeight": 8192}
        if mode: manager.set_render_mode(mode)
        return manager

    def close(self, manager):
        manager.viewer = None
        manager.direct = False
        try: manager.cleanup()
        finally: manager.lock.close()

    def test_mode_persists_and_is_blocked_while_viewing(self):
        fake = CanvasHypr()
        manager = self.manager(fake)
        try:
            self.assertEqual(manager.render_mode, "monitors")
            manager.apply(default_layout())
            manager.set_render_mode("canvas")
            # Outputs of the other mode never outlive the switch.
            self.assertFalse(manager.owned)
            self.assertEqual(json.loads(manager.presentation_profile.read_text())["renderMode"], "canvas")
            with self.assertRaises(ValueError): manager.set_render_mode("stereo")
            process = Mock(); process.poll.return_value = None; manager.viewer = process
            with self.assertRaisesRegex(RuntimeError, "Stop XR before switching the render mode."):
                manager.set_render_mode("monitors")
            self.assertEqual(manager.render_mode, "canvas")
        finally: self.close(manager)
        again = self.manager(CanvasHypr())
        try: self.assertTrue(again.canvas_mode)
        finally: self.close(again)

    def test_canvas_mode_requires_controls_v6(self):
        manager = self.manager(CanvasHypr())
        try:
            (self.runtime / "controls.version").write_text("5\n")
            with self.assertRaisesRegex(RuntimeError, "XR controls need setup"): manager.set_render_mode("canvas")
            self.assertEqual(manager.render_mode, "monitors")
            process = Mock(); process.poll.return_value = None; manager.viewer = process
            self.assertIn("XR controls need setup", manager.controls_hint())
            (self.runtime / "controls.version").unlink()
            self.assertEqual(manager.controls_version(), 0)
            (self.runtime / "controls.version").write_text("6\n")
            self.assertEqual(manager.controls_hint(), "")
            manager.viewer = None
            manager.set_render_mode("canvas")
            self.assertTrue(manager.canvas_mode)
        finally: self.close(manager)

    def test_viewer_command_per_mode(self):
        fake = CanvasHypr()
        manager = self.manager(fake)
        pose = str(manager.pose_socket)
        try:
            manager.apply(default_layout())
            base = ["/unused", "--layout", str(manager.directory / "viewer.tsv"), "--fps", "60", "--workspace-curvature", "0",
                    "--spacing", "24", "--pose-socket", pose]
            self.assertEqual(manager.viewer_command(False, False), base)
            with patch("backend.detect", return_value={"displays": ["DP-1"]}):
                self.assertEqual(manager.viewer_command(True, False), base + ["--display", "DP-1"])
            manager.dedicated.output = "DP-1"
            self.assertEqual(manager.viewer_command(True, True), base + ["--direct", "DP-1", "--stereo"])
            manager.set_render_mode("canvas")
            with self.assertRaisesRegex(RuntimeError, "Start the window canvas first"): manager.viewer_command(False, False)
            manager.canvas.ensure(manager.monitors())
            canvas = ["/unused", "--canvas", str(manager.directory / "canvas.tsv"), "--fps", "60", "--pose-socket", pose]
            self.assertEqual(manager.viewer_command(False, False), canvas)
            with patch("backend.detect", return_value={"displays": ["DP-1"]}):
                self.assertEqual(manager.viewer_command(True, False), canvas + ["--display", "DP-1"])
            self.assertEqual(manager.viewer_command(True, True), canvas + ["--direct", "DP-1", "--stereo"])
            self.assertIsNone(manager.applied)
        finally: self.close(manager)

    def test_ensure_creates_one_output_and_touches_no_layout(self):
        for seeded in (False, True):
            with self.subTest(seeded=seeded):
                fake = CanvasHypr()
                manager = self.manager(fake, "canvas")
                layout = json.dumps(default_layout()).encode()
                if seeded: manager.profile.write_bytes(layout)
                try:
                    manager.canvas.ensure(manager.monitors())
                    name = manager.prefix + "canvas"
                    self.assertEqual(set(fake.outputs), {"eDP-1", name})
                    self.assertEqual({k: fake.outputs[name][k] for k in ("width", "height", "refreshRate", "scale", "x", "y")},
                                     dict(width=2560, height=1440, refreshRate=60, scale=1.0, x=2020, y=0))
                    self.assertEqual(json.loads(manager.journal.read_text()), [name])
                    self.assertTrue(manager.canvas.active)
                    for w in ("omxr-canvas", "omxr-park"):
                        self.assertTrue(fake.evals(f'hl.workspace_rule({{workspace="name:{w}", monitor="{name}", persistent=true}})'))
                        self.assertTrue(fake.evals(f'match={{workspace="name:{w}"}}, float=true'))
                    self.assertTrue(fake.evals('suppress_event="fullscreen maximize"'))
                    self.assertEqual((manager.directory / "canvas.tsv").read_text().splitlines()[0], GOLDEN)
                    manager.stop_viewer()
                    self.assertEqual(set(fake.outputs), {"eDP-1"})
                    self.assertTrue(fake.evals("omarchy_xr_canvas_rules=nil"))
                    self.assertEqual(manager.profile.read_bytes() if seeded else manager.profile.exists(), layout if seeded else False)
                    self.assertFalse((manager.directory / "viewer.tsv").exists())
                    self.assertIsNone(manager.applied)
                finally: self.close(manager)

    def test_settings_roundtrip_and_tsv(self):
        fake = CanvasHypr()
        manager = self.manager(fake, "canvas")
        try:
            self.assertEqual(manager.canvas.load(), DEFAULTS)
            manager.canvas.publish()
            lines = (manager.directory / "canvas.tsv").read_text().splitlines()
            self.assertEqual(lines, [GOLDEN, f"exclude {os.getpid()}", f"exclude {os.getppid()}"])
            saved = manager.canvas.save({"refresh": 120, "outputScale": 1.25, "exclude": ["firefox"], "radius": 3})
            self.assertEqual(manager.canvas.load(), saved)
            lines = (manager.directory / "canvas.tsv").read_text().splitlines()
            self.assertEqual(lines[:3], ["# canvas v1 60 3 60 0.35 0.8 1.25 300 all 1", "exclude firefox", f"exclude {os.getpid()}"])
            manager.canvas.save({**saved, "takeoverKeys": False})
            header = (manager.directory / "canvas.tsv").read_text().splitlines()[0]
            self.assertEqual(header, "# canvas v1 60 3 60 0.35 0.8 1.25 300 all 0")
            # The Lua adapter's adopt-policy pattern still finds field 8 with field 9 appended.
            self.assertEqual(re.match(r"^# canvas v1(?:\s+\S+){7}\s+([A-Za-z-]+)", header).group(1), "all")
            saved = manager.canvas.save({**saved, "takeoverKeys": True})
            manager.canvas.ensure(manager.monitors())
            self.assertTrue(fake.evals('mode="2560x1440@120"'))
            self.assertTrue(fake.evals("scale=1.25"))
            for bad in ({"fps": 0}, {"fps": True}, {"fps": 30.5}, {"radius": 11}, {"gapPx": -1}, {"dimUnmatched": 2},
                        {"labelDeg": 0}, {"outputScale": 1.5}, {"outputScale": True}, {"refresh": 90},
                        {"captureBudgetMpix": 10}, {"adoptPolicy": "some"}, {"takeoverKeys": 1},
                        {"exclude": ["bad token"]}, {"exclude": "firefox"}, {"unknown": 1}, []):
                with self.subTest(bad=bad), self.assertRaises(ValueError): validate(bad)
            self.assertEqual(manager.canvas.load(), saved)
        finally: self.close(manager)

    def test_migration_journals_before_moving_and_restores_per_window(self):
        fake = CanvasHypr()
        manager = self.manager(fake, "canvas")
        fake.clients = [client("0xa1", 1), client("0xb2", 2, floating=True, at=[300, 400], size=[640, 480]),
                        client("0xc3", 1, **{"class": "omarchy-xr-spectator"}),
                        client("0xd4", {"id": -99, "name": "special:scratch"}),
                        client("0xe5", 1, mapped=False), client("0xf6", 1, pid=os.getpid()),
                        client("0xa7", 1, title="Omarchy XR")]
        skipped = {c["address"]: json.dumps(c) for c in fake.clients[2:]}
        seen = []
        fake.on_move = lambda: seen.append(manager.canvas.journal.exists())
        try:
            manager.canvas.ensure(manager.monitors())
            self.assertTrue(seen and all(seen))
            journal = json.loads(manager.canvas.journal.read_text())
            self.assertEqual(journal, {"session": "test-session", "windows": {
                "0xa1": {"workspace": 1, "floating": False, "size": [800, 600], "at": [100, 200]},
                "0xb2": {"workspace": 2, "floating": True, "size": [640, 480], "at": [300, 400]}}})
            self.assertEqual([c["workspace"]["name"] for c in fake.clients[:2]], ["omxr-park", "omxr-park"])
            self.assertTrue(fake.evals('window.float({window="address:0xa1", action="float"})'))
            self.assertTrue(fake.evals('window.resize({window="address:0xa1", x=800, y=600})'))
            self.assertFalse(fake.evals('window.float({window="address:0xb2"'))
            self.assertEqual({c["address"]: json.dumps(c) for c in fake.clients[2:]}, skipped)
            self.assertFalse([e for e in fake.evals("window.move") if "workspace=" in e and "follow=false" not in e])
            fake.window("0xb2")["at"] = [2100, 0]
            manager.stop_viewer()
            a, b = fake.window("0xa1"), fake.window("0xb2")
            self.assertEqual((a["workspace"]["id"], a["floating"]), (1, False))
            self.assertEqual((b["workspace"]["id"], b["floating"], b["at"], b["size"]), (2, True, [300, 400], [640, 480]))
            self.assertFalse(manager.canvas.journal.exists())
            self.assertEqual(set(fake.outputs), {"eDP-1"})
            self.assertIsNone(manager.applied)
            # Stop drops the per-window decoration overrides Lua set on the canvas.
            for address in ("0xa1", "0xb2"):
                for prop in ("border_size", "rounding", "no_anim", "no_shadow", "no_blur", "no_dim"):
                    self.assertTrue(fake.evals(f'set_prop({{window="address:{address}", prop="{prop}", value="unset"}})'), (address, prop))
            self.assertFalse(fake.evals('address:0xc3", prop='))
        finally: self.close(manager)

    def test_exclusions_are_never_adopted(self):
        fake = CanvasHypr()
        manager = self.manager(fake, "canvas")
        fake.clients = [client("0xa1", 1), client("0xb2", 1, **{"class": "firefox"}), client("0xc3", 2, pid=4343)]
        try:
            manager.canvas.save({"exclude": ["firefox", "4343"]})
            manager.canvas.ensure(manager.monitors())
            self.assertEqual(list(json.loads(manager.canvas.journal.read_text())["windows"]), ["0xa1"])
            self.assertEqual([c["workspace"]["name"] for c in fake.clients], ["omxr-park", "1", "2"])
            manager.redistribute_laptop_windows({1, 2}, manager.monitors())
            self.assertEqual([c["workspace"]["name"] for c in fake.clients], ["omxr-park", "1", "2"])
        finally: self.close(manager)

    def test_canvas_monitor_identity_is_reserved(self):
        layout = default_layout()
        for reserved in ("canvas", "left-canvas"):
            layout["monitors"][0]["id"] = reserved
            with self.subTest(reserved=reserved), self.assertRaisesRegex(ValueError, "reserved for the window canvas"):
                validate_layout(layout)
        layout["monitors"][0]["id"] = "canvas2"
        validate_layout(layout)

    def test_unjournaled_leftovers_return_to_the_laptop(self):
        fake = CanvasHypr()
        manager = self.manager(fake, "canvas")
        try:
            manager.canvas.ensure(manager.monitors())
            fake.clients = [client("0xab", "omxr-canvas", floating=True)]
            manager.stop_viewer()
            self.assertEqual(fake.window("0xab")["workspace"]["id"], 1)
            self.assertTrue(fake.evals('set_prop({window="address:0xab", prop="no_dim", value="unset"})'))
        finally: self.close(manager)

    def test_adopt_policy_empty_skips_migration(self):
        fake = CanvasHypr()
        manager = self.manager(fake, "canvas")
        fake.clients = [client("0xa1", 1)]
        try:
            manager.canvas.save({"adoptPolicy": "empty"})
            manager.canvas.ensure(manager.monitors())
            self.assertFalse(fake.evals("window.move"))
            self.assertFalse(manager.canvas.journal.exists())
            self.assertTrue((manager.directory / "canvas.tsv").read_text().splitlines()[0].endswith(" empty 1"))
        finally: self.close(manager)

    def test_laptop_off_adoption_targets_park(self):
        fake = CanvasHypr()
        manager = self.manager(fake, "canvas")
        manager.canvas.save({"adoptPolicy": "empty"})
        try:
            manager.canvas.ensure(manager.monitors())
            fake.clients = [client("0xa1", 1), client("0xb2", 2), client("bogus", 1)]
            manager.redistribute_laptop_windows({1}, manager.monitors())
            moves = fake.evals("window.move")
            self.assertEqual(moves, ['hl.dispatch(hl.dsp.window.move({window="address:0xa1", workspace="name:omxr-park", follow=false}))'])
            self.assertEqual(list(json.loads(manager.canvas.journal.read_text())["windows"]), ["0xa1"])
            manager.stop_viewer()
            self.assertEqual(fake.window("0xa1")["workspace"]["id"], 1)
        finally: self.close(manager)

    def test_relocate_never_targets_canvas_output(self):
        fake = CanvasHypr()
        manager = self.manager(fake)
        try:
            canvas, panel = manager.prefix + "canvas", manager.prefix + "1"
            manager.owned = {canvas, panel}
            monitors = [{"name": canvas, "width": 2560}, {"name": panel, "width": 1920}, {"name": "eDP-1", "width": 1920}]
            fake.workspaces = [{"id": 3, "monitor": panel, "windows": 1},
                               {"id": -1338, "name": "omxr-canvas", "monitor": panel, "windows": 0}]
            manager.relocate_workspaces({panel}, {panel}, monitors)
            self.assertEqual([w["monitor"] for w in fake.workspaces], ["eDP-1", panel])
            with self.assertRaisesRegex(RuntimeError, "no computer display"):
                manager.relocate_workspaces({panel, "eDP-1"}, {panel}, monitors)
            manager.owned = set()
        finally: self.close(manager)

    def test_reconcile_reinstalls_rules_after_reload(self):
        fake = CanvasHypr()
        manager = self.manager(fake, "canvas")
        try:
            manager.canvas.ensure(manager.monitors())
            name = manager.canvas.name
            fake.calls.clear()
            manager.reconcile_outputs(manager.monitors())
            self.assertFalse(fake.evals())
            fake.workspaces = [{"id": -1338, "name": "omxr-canvas", "monitor": "eDP-1", "windows": 0}]
            manager.reconcile_outputs(manager.monitors())
            self.assertTrue(fake.evals(f'hl.monitor({{output="{name}", mode="2560x1440@60", position="2020x0", scale=1}})'))
            self.assertTrue(fake.evals("omarchy_xr_canvas_rules={hl.workspace_rule"))
            self.assertEqual(fake.workspaces[0]["monitor"], name)
            fake.calls.clear()
            fake.outputs[name].update(scale=2.0, x=0)
            manager.status()
            self.assertEqual((fake.outputs[name]["scale"], fake.outputs[name]["x"]), (1.0, 2020))
            fake.calls.clear()
            manager.reconcile_outputs(manager.monitors())
            self.assertFalse(fake.evals())
            del fake.outputs[name]
            with self.assertRaisesRegex(RuntimeError, "window canvas output disconnected"):
                manager.reconcile_outputs(manager.monitors())
            fake.outputs[name] = {"name": name, **fake.rules[name]}
        finally: self.close(manager)

    def test_recovery_restores_windows_before_removing_output(self):
        fake = CanvasHypr()
        stale = "OMXR-deadbeef-canvas"
        fake.outputs[stale] = {"name": stale, "width": 2560, "height": 1440, "x": 2020, "y": 0, "scale": 1, "refreshRate": 60}
        fake.clients = [client("0xa1", "omxr-park", floating=True), client("0xb2", "omxr-canvas", floating=True)]
        self.state.mkdir()
        (self.state / "outputs.json").write_text(json.dumps([stale]))
        (self.state / "canvas-session.json").write_text(json.dumps({"session": "test-session", "windows": {
            "0xa1": {"workspace": 4, "floating": False, "size": [800, 600], "at": [0, 0]}}}))
        manager = self.manager(fake)
        try:
            moves = [i for i, a in enumerate(fake.calls) if a[0] == "eval" and "window.move" in a[1]]
            removal = fake.calls.index(("output", "remove", stale))
            self.assertTrue(moves and max(moves) < removal)
            self.assertEqual((fake.window("0xa1")["workspace"]["id"], fake.window("0xa1")["floating"]), (4, False))
            self.assertEqual(fake.window("0xb2")["workspace"]["id"], 1)
            self.assertTrue(fake.evals("omarchy_xr_canvas_rules=nil"))
            self.assertFalse((self.state / "canvas-session.json").exists())
            self.assertFalse(manager.owned)
            self.assertEqual(manager.restoration_error, "")
        finally: self.close(manager)

    def test_recovery_drops_a_journal_from_another_compositor_session(self):
        fake = CanvasHypr()
        fake.clients = [client("0xa1", 2)]
        self.state.mkdir()
        (self.state / "canvas-session.json").write_text(json.dumps({"session": "old", "windows": {
            "0xa1": {"workspace": 4, "floating": False, "size": [800, 600], "at": [0, 0]}}}))
        manager = self.manager(fake)
        try:
            self.assertFalse(fake.evals("window.move"))
            self.assertFalse((self.state / "canvas-session.json").exists())
        finally: self.close(manager)

    def test_terminal_opens_on_canvas_workspace(self):
        fake = CanvasHypr()
        manager = self.manager(fake, "canvas")
        try:
            with self.assertRaisesRegex(RuntimeError, "Start the window canvas first"): manager.terminal("1")
            manager.canvas.ensure(manager.monitors())
            manager.terminal("1")
            self.assertEqual(fake.evals("exec_cmd"), ['hl.exec_cmd("foot", {workspace="name:omxr-canvas silent"})'])
        finally: self.close(manager)

    def test_status_fields(self):
        fake = CanvasHypr()
        manager = self.manager(fake)
        try:
            status = manager.status()
            self.assertEqual((status["renderMode"], status["canvasActive"], status["controlsVersion"], status["canvasWindows"]),
                             ("monitors", False, 6, 0))
            manager.set_render_mode("canvas")
            manager.canvas.ensure(manager.monitors())
            process = Mock(); process.poll.return_value = None; process.pid = 123; manager.viewer = process
            Path(str(manager.pose_socket) + ".stats").write_text(json.dumps({"pid": 123, "time": time.monotonic(), "canvasWindows": 4}))
            status = manager.status()
            self.assertEqual((status["renderMode"], status["canvasActive"], status["active"], status["canvasWindows"]),
                             ("canvas", True, 1, 4))
            Path(str(manager.pose_socket) + ".stats").write_text(json.dumps({"pid": 123, "time": time.monotonic(), "canvasState": "search"}))
            self.assertEqual(manager.status()["canvasState"], "search")
        finally: self.close(manager)

    def test_canvas_camera_verbs(self):
        manager = self.manager(CanvasHypr())
        try:
            process = Mock(); process.poll.return_value = None; manager.viewer = process
            pose = str(manager.pose_socket)
            with patch("backend.socket.socket") as sock:
                sendto = sock.return_value.__enter__.return_value.sendto
                with self.assertRaisesRegex(ValueError, "Only available in Window canvas mode"): perform(manager, {"action": "search"})
                sendto.assert_not_called()
                perform(manager, {"action": "fit"})
                sendto.assert_called_with(b"fit", pose)
                manager.render_mode = "canvas"
                messages = {"overview": "Overview toggled.", "search": "Search opened.", "fill": "Fill toggled.", "arrange": "Windows arranged.",
                            "undo": "Undone.", "redo": "Redone.", "pin": "Pin toggled.", "help": "Help toggled."}
                for action, message in messages.items():
                    with self.subTest(action=action):
                        self.assertEqual(perform(manager, {"action": action})["message"], message)
                        sendto.assert_called_with(action.encode(), pose)
                perform(manager, {"action": "fit"})
                sendto.assert_called_with(b"fit", pose)
                with self.assertRaisesRegex(ValueError, "Unknown camera action"): manager.camera_control("focus:0x1")
        finally: self.close(manager)

    def test_actions_route_render_mode_and_settings(self):
        fake = CanvasHypr()
        manager = self.manager(fake)
        try:
            self.assertEqual(perform(manager, {"action": "load"})["canvas"], DEFAULTS)
            perform(manager, {"action": "set_render_mode", "renderMode": "canvas"})
            self.assertTrue(manager.canvas_mode)
            response = perform(manager, {"action": "apply", "layout": default_layout()})
            self.assertEqual(response["canvas"], DEFAULTS)
            self.assertIsNone(manager.applied)
            response = perform(manager, {"action": "set_canvas_settings", "canvas": {"refresh": 120}})
            self.assertEqual(response["canvas"]["refresh"], 120)
            self.assertEqual(fake.outputs[manager.canvas.name]["refreshRate"], 120)
            manager.canvas.profile.write_text("{broken")
            response = perform(manager, {"action": "load"})
            self.assertEqual((response["canvas"], response["message"]), (DEFAULTS, "Canvas settings were invalid and were set aside"))
            self.assertTrue(manager.canvas.profile.with_name("canvas.json.corrupt").exists())
        finally: self.close(manager)


if __name__ == "__main__":
    unittest.main()
