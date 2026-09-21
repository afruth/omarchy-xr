import copy
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"studio"))
from backend import Manager, default_layout, validate, add_gutters, serve, effective_scale
from sdk import SDK
import io

def save_two_setups(test, manager):
    layout = default_layout()
    layout.update(curvature=72, spacing=30, fps=30)
    layout["monitors"][0]["curvature"] = 49
    layout = add_gutters(layout)
    manager.save_setup("Coding / focus", layout)
    item = manager.setups()["items"][0]
    test.assertEqual(item["layout"], layout)
    # No outputs are created when selecting a setup before startup.
    manager.use_setup(item["id"])
    test.assertFalse(manager.owned)
    test.assertEqual(manager.load(), layout)
    manager.apply(layout)
    other = copy.deepcopy(layout)
    other["monitors"] = other["monitors"][:2]
    other["monitors"][0]["width"] = 1280
    other["curvature"] = 10
    manager.save_setup("Reading", other)
    return layout, item, other


def reject_setup_names(test, manager, layout):
    for name in ("", "  ", "x" * 81, "a\nb", None, "coding / FOCUS"):
        with test.assertRaises(ValueError):
            manager.save_setup(name, layout)


class FakeHypr:
    def __init__(self):
        self.outputs={"eDP-1":{"name":"eDP-1","width":1920,"height":1080,"x":0,"y":0,"scale":1}}
        self.fail=False
        self.workspaces=[]
    def move_workspace(self, command):
        identity, target = re.search(r'workspace=(-?\d+), monitor="([^"]+)"', command).groups()
        for workspace in self.workspaces:
            if workspace["id"] == int(identity):
                workspace["monitor"] = target
    def __call__(self,*args):
        if args==("-j","monitors"): return json.dumps(list(self.outputs.values()))
        if args==("-j","workspaces"): return json.dumps(self.workspaces)
        if args[:2]==("output","create"):
            name=args[3]; self.outputs[name]={"name":name,"width":1920,"height":1080,"x":1920,"y":0,"scale":1};return "ok"
        if args[:2]==("output","remove"):
            del self.outputs[args[2]];return "ok"
        if args[0]=="eval":
            if self.fail: raise RuntimeError("Injected compositor failure")
            if 'hl.dsp.workspace.move' in args[1]:
                self.move_workspace(args[1])
                return "ok"
            name,w,h,x,y=re.search(r'output="([^"]+)", mode="(\d+)x(\d+)@60", position="(-?\d+)x(-?\d+)"',args[1]).groups()
            if name in self.outputs:
                self.outputs[name].update(width=int(w),height=int(h),x=int(x),y=int(y),scale=float(re.search(r"scale=([0-9.]+)",args[1])[1]))
            return "ok"
        raise AssertionError(args)

class LayoutTests(unittest.TestCase):
    def test_layout_transitions_never_overlap_even_before_obsolete_outputs_are_removed(self):
        class CheckedHypr(FakeHypr):
            def __init__(self):
                super().__init__()
                self.rules = {}
            def __call__(self, *args):
                if args[0] == "eval":
                    name,w,h,x,y=re.search(r'output="([^"]+)", mode="(\d+)x(\d+)@60", position="(-?\d+)x(-?\d+)"',args[1]).groups()
                    self.rules[name] = dict(name=name,width=int(w),height=int(h),x=int(x),y=int(y),
                                           scale=float(re.search(r"scale=([0-9.]+)",args[1])[1]))
                result = super().__call__(*args)
                if args[:2] == ("output", "create"):
                    self.outputs[args[3]].update(self.rules[args[3]])
                values = list(self.outputs.values())
                for i, a in enumerate(values):
                    for b in values[i+1:]:
                        ax,ay,ar,ab = Manager.output_rect(a)
                        bx,by,br,bb = Manager.output_rect(b)
                        if ax<br and bx<ar and ay<bb and by<ab:
                            raise AssertionError(f'Transient overlap: {a["name"]}, {b["name"]}')
                return result
        with tempfile.TemporaryDirectory() as temp:
            fake=CheckedHypr();manager=Manager(temp,"/unused",fake)
            manager.graphics_limits={"maxWidth":8192,"maxHeight":8192}
            try:
                layout=default_layout();manager.apply(layout)
                # Grow the first panel across the two panels being removed.
                wide=copy.deepcopy(layout);wide["monitors"]=wide["monitors"][:1]
                wide["monitors"][0]["width"]=5120
                manager.apply(wide)
                manager.apply(layout)
                # Swap two existing outputs, then replace all output identities.
                swapped=copy.deepcopy(layout)
                a,b=swapped["monitors"][:2];a["x"],b["x"]=b["x"],a["x"]
                manager.apply(swapped)
                for m in swapped["monitors"]:m["id"] += "new"
                manager.apply(swapped)
                self.assertEqual(len(manager.owned),len(swapped["monitors"]))
            finally:manager.cleanup();manager.lock.close()

    def test_external_reload_repairs_modes_without_restarting_viewer(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake)
            manager.graphics_limits={"maxWidth":8192,"maxHeight":8192}
            try:
                layout=default_layout();layout["monitors"][0].update(width=3440,height=1440,scale=1.25)
                manager.apply(layout)
                expected=copy.deepcopy(fake.outputs)
                process=Mock();process.poll.return_value=None;manager.viewer=process;manager.direct=True
                for name in manager.owned:fake.outputs[name].update(width=1920,height=1080,scale=2,x=0)
                manager.reconcile_outputs(manager.monitors())
                self.assertEqual(fake.outputs,expected)
                process.terminate.assert_not_called()
                # Applying the same layout must also repair a reset, not skip on names alone.
                fake.outputs[manager.prefix+"1"].update(width=1920,height=1080)
                manager.apply(layout)
                self.assertEqual(fake.outputs,expected)
            finally:
                manager.viewer=None;manager.direct=False;manager.cleanup();manager.lock.close()

    def test_laptop_windows_move_to_visible_xr_workspaces(self):
        with tempfile.TemporaryDirectory() as temp:
            manager=Manager(temp,"/unused",FakeHypr())
            manager.owned={"OMXR-a","OMXR-b"}
            monitors=[{"name":"eDP-1","width":1920},
                      {"name":"OMXR-a","activeWorkspace":{"id":3}},
                      {"name":"OMXR-b","activeWorkspace":{"id":4}}]
            workspaces=[{"id":1,"monitor":"eDP-1"},{"id":2,"monitor":"eDP-1"}]
            clients=[{"address":"0x"+str(i),"workspace":{"id":ws}} for i,ws in enumerate([1,2,1,9, -1])]
            def runner(*args):
                if args==("-j","workspaces"):return json.dumps(workspaces)
                if args==("-j","clients"):return json.dumps(clients)
                return "ok"
            manager.runner=Mock(side_effect=runner)
            try:
                manager.reconcile_laptop_workspaces(monitors)
                manager.reconcile_laptop_workspaces(monitors[1:])
                moves=[c.args[1] for c in manager.runner.call_args_list if c.args[0]=="eval"]
                self.assertEqual(len(moves),3)
                for command,target in zip(moves,[3,4,3]):
                    self.assertIn('workspace="'+str(target)+'"',command)
                    self.assertIn('follow=false',command)
                manager.runner.reset_mock()
                manager.reconcile_laptop_workspaces(monitors[1:])
                self.assertFalse(any(c.args[0]=="eval" for c in manager.runner.call_args_list))
                manager.reconcile_laptop_workspaces(monitors,disabling=True)
                self.assertEqual(sum(c.args[0]=="eval" for c in manager.runner.call_args_list),3)
            finally:manager.lock.close()

    def test_stop_returns_all_xr_workspaces_after_restoring_laptop(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake)
            manager.apply(default_layout())
            panel=fake.outputs.pop("eDP-1")
            fake.outputs["DP-1"]={**panel,"name":"DP-1","description":"VITURE"}
            names=sorted(manager.owned)
            fake.workspaces=[{"id":identity,"name":label,"monitor":names[i % len(names)],"windows":2}
                             for i,(identity,label) in enumerate([(3,"3"),(4,"4"),(-1337,"work"),(-99,"special:notes")])]
            expected=copy.deepcopy(fake.workspaces)
            manager.laptop.stop=Mock(side_effect=lambda:fake.outputs.update({"eDP-1":panel}))
            process=Mock();process.poll.return_value=None;manager.viewer=process
            try:
                manager.stop_viewer()
                process.terminate.assert_called_once()
                self.assertEqual(fake.workspaces,[{**w,"monitor":"eDP-1"} for w in expected])
                self.assertEqual(set(fake.outputs),{"eDP-1","DP-1"})
                self.assertEqual(json.loads(manager.journal.read_text()),[])
                self.assertFalse(manager.owned)
                self.assertIsNone(manager.applied)
                self.assertEqual(manager.load(),default_layout())
                manager.stop_viewer()  # Repeated release is harmless.
            finally:manager.lock.close()

    def test_release_failure_keeps_outputs_and_journal_for_retry(self):
        for cause in ("missing-display", "migration-failure", "migration-ignored", "removal-failure"):
            with self.subTest(cause=cause), tempfile.TemporaryDirectory() as temp:
                fake=FakeHypr();manager=Manager(temp,"/unused",fake)
                manager.apply(default_layout())
                owned=set(manager.owned)
                fake.workspaces=[{"id":3,"monitor":sorted(owned)[0],"windows":1}]
                panel=fake.outputs.pop("eDP-1") if cause=="missing-display" else None
                fake.fail=cause=="migration-failure"
                def runner(*args):
                    if cause=="migration-ignored" and args[0]=="eval":return "ok"
                    if cause=="removal-failure" and args[:2]==("output","remove"):
                        raise RuntimeError("Removal failed")
                    return fake(*args)
                manager.runner=runner
                try:
                    with self.assertRaisesRegex(RuntimeError,"Workspace release"):
                        manager.stop_viewer()
                    self.assertEqual(manager.owned,owned)
                    self.assertEqual(set(json.loads(manager.journal.read_text())),owned)
                    fake.fail=False;manager.runner=fake
                    if panel:fake.outputs["eDP-1"]=panel
                    manager.stop_viewer()
                    self.assertFalse(manager.owned)
                    self.assertEqual(fake.workspaces[0]["monitor"],"eDP-1")
                finally:manager.lock.close()

    def test_crash_recovery_releases_journaled_workspaces(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake)
            manager.apply(default_layout())
            fake.workspaces=[{"id":3,"monitor":sorted(manager.owned)[0],"windows":2}]
            manager.lock.close()  # Simulate a dead worker with outputs still alive.
            recovered=Manager(temp,"/unused",fake)
            try:
                self.assertFalse(recovered.owned)
                self.assertEqual(set(fake.outputs),{"eDP-1"})
                self.assertEqual(fake.workspaces,[{"id":3,"monitor":"eDP-1","windows":2}])
            finally:recovered.lock.close()

    def test_viewer_exit_releases_workspaces(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake)
            manager.apply(default_layout())
            fake.workspaces=[{"id":3,"monitor":sorted(manager.owned)[0],"windows":1}]
            manager.viewer=Mock();manager.viewer.poll.return_value=1
            manager.sdk.status=Mock(return_value={})
            try:
                status=manager.status()
                self.assertFalse(status["viewing"])
                self.assertEqual(status["active"],0)
                self.assertEqual(fake.workspaces[0]["monitor"],"eDP-1")
            finally:manager.lock.close()

    def test_internal_viewer_handoff_keeps_applied_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();renderer=Path(temp)/"renderer";renderer.touch()
            manager=Manager(temp,renderer,fake)
            manager.apply(default_layout());owned=set(manager.owned)
            process=Mock();process.poll.return_value=None
            try:
                with patch("backend.subprocess.Popen",return_value=process), patch("backend.time.sleep"):
                    manager.start()
                self.assertEqual(manager.owned,owned)
                self.assertIsNotNone(manager.applied)
            finally:manager.cleanup();manager.lock.close()

    def test_layout_shrink_moves_workspaces_to_surviving_xr_output(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake)
            layout=default_layout();manager.apply(layout)
            fake.workspaces=[{"id":4,"monitor":manager.prefix+layout["monitors"][-1]["id"],"windows":1}]
            layout["monitors"]=layout["monitors"][:1]
            try:
                manager.apply(layout)
                self.assertEqual(fake.workspaces[0]["monitor"],manager.prefix+layout["monitors"][0]["id"])
            finally:manager.cleanup();manager.lock.close()

    def test_explicit_workspace_degrees_roundtrip(self):
        with tempfile.TemporaryDirectory() as temp:
            manager=Manager(temp,"/unused",FakeHypr())
            manager.graphics_limits={"maxWidth":8192,"maxHeight":8192}
            try:
                layout=default_layout();layout["workspaceDegrees"]=360;layout["workspaceFollow"]=True
                manager.apply(layout)
                self.assertEqual(manager.load()["workspaceDegrees"],360)
                self.assertTrue(manager.load()["workspaceFollow"])
                self.assertTrue((Path(temp)/"viewer.tsv").read_text().splitlines()[0].endswith(" 360 1"))
                for value in (-2,361,True,float("nan")):
                    layout["workspaceDegrees"]=value
                    with self.assertRaises(ValueError):validate(layout)
            finally:manager.cleanup();manager.lock.close()

    def test_text_size_uses_omarchy_and_rejects_invalid_values(self):
        manager = Mock(); manager.status.return_value = {}
        requests = "\n".join(json.dumps({"action":"set_text_size","textSize":value}) for value in (14,8,21,True,"14"))
        output = io.StringIO()
        with patch("sys.stdin",io.StringIO(requests)), patch("sys.stdout",output), patch("backend.subprocess.run") as run:
            serve(manager)
            run.assert_called_once_with(["omarchy-display-text-size","14"],check=True,capture_output=True,text=True,timeout=15)
        self.assertEqual([json.loads(line)["ok"] for line in output.getvalue().splitlines()],[True,False,False,False,False])

    def test_scale_brightness_live_and_saved(self):
        with tempfile.TemporaryDirectory() as temp:
            fake = FakeHypr()
            manager = Manager(temp,"/unused",fake)
            manager.graphics_limits = {"maxWidth":8192,"maxHeight":8192}
            try:
                layout = default_layout()
                manager.apply(layout)
                process = Mock(); process.poll.return_value = None
                manager.viewer = process; manager.direct = True
                layout["monitors"][0].update(scale=1.25, brightness=45)
                manager.apply(layout)
                self.assertEqual(fake.outputs[manager.prefix+"1"]["scale"],1.25)
                process.terminate.assert_not_called()
                self.assertIn("\t45\n",(Path(temp)/"viewer.tsv").read_text())
                manager.save_setup("Scaled",layout)
                self.assertEqual(manager.setups()["items"][0]["layout"],layout)
                # Brightness-only edits must not reconfigure outputs.
                manager.runner = Mock(wraps=fake)
                layout["monitors"][0]["brightness"] = 70
                manager.apply(layout)
                self.assertFalse(any(c.args[0] == "eval" for c in manager.runner.call_args_list))
                for value in (0, 101, True, float("nan")):
                    bad = copy.deepcopy(layout); bad["monitors"][0]["brightness"] = value
                    with self.assertRaises(ValueError): validate(bad)
                bad = copy.deepcopy(layout); bad["monitors"][0]["scale"] = 0
                with self.assertRaises(ValueError): validate(bad)
                self.assertEqual(effective_scale({"width":1366,"height":768,"scale":1.25}), 2)
            finally:
                manager.viewer = None; manager.direct = False
                manager.cleanup(); manager.lock.close()

    def test_spectator_live_toggle_and_placement(self):
        with tempfile.TemporaryDirectory() as temp:
            runner=Mock(return_value="ok")
            manager=Manager(temp,"/unused",runner)
            manager.monitors=Mock(return_value=[
                {"name":"OMXR-other-1","activeWorkspace":{"id":7}},
                {"name":"eDP-1","activeWorkspace":{"id":1}}])
            process=Mock(); process.poll.return_value=None
            manager.viewer=process; manager.direct=True
            try:
                with patch("backend.socket.socket") as socket:
                    manager.set_spectator(True)
                    self.assertIn('workspace="1 silent"', runner.call_args.args[1])
                    socket.return_value.__enter__.return_value.sendto.assert_called_with(b"spectator_on", str(Path(temp)/"pose.sock"))
                    self.assertTrue(json.loads(manager.presentation_profile.read_text())["spectator"])
                    manager.set_spectator(False)
                    socket.return_value.__enter__.return_value.sendto.assert_called_with(b"spectator_off", str(Path(temp)/"pose.sock"))
                    process.terminate.assert_not_called()
                    with self.assertRaises(ValueError): manager.set_spectator("yes")
                manager.monitors.return_value=[{"name":"OMXR-other-1"}]
                with self.assertRaisesRegex(RuntimeError,"computer display"): manager.set_spectator(True)
            finally:
                manager.viewer=None; manager.direct=False; manager.cleanup(); manager.lock.close()

    def test_saved_setups_roundtrip_and_live_switch(self):
        with tempfile.TemporaryDirectory() as temp:
            manager=Manager(temp,"/unused",FakeHypr())
            try:
                layout, item, other = save_two_setups(self, manager)
                second=manager.setups()["items"][1]
                process=Mock();process.poll.return_value=None
                manager.viewer=process;manager.direct=True
                manager.use_setup(second["id"])
                self.assertIs(manager.viewer,process)
                process.terminate.assert_not_called()
                self.assertEqual(manager.applied,other)
                self.assertEqual(len(manager.owned),2)
                self.assertEqual(manager.setups()["items"][0]["layout"],layout)
                manager.save_setup("Reading renamed",layout,second["id"])
                self.assertEqual(len(manager.setups()["items"]),2)
                self.assertEqual(manager.setups()["items"][1]["name"],"Reading renamed")
                before=(Path(temp)/"setups.json").read_text()
                reject_setup_names(self, manager, layout)
                with self.assertRaises(ValueError):manager.use_setup("missing")
                with self.assertRaises(ValueError):manager.save_setup("New",layout,"missing")
                self.assertEqual((Path(temp)/"setups.json").read_text(),before)
                with patch.object(manager,"apply",side_effect=RuntimeError("failed")):
                    with self.assertRaises(RuntimeError):manager.use_setup(item["id"])
                self.assertEqual((Path(temp)/"setups.json").read_text(),before)
            finally:
                manager.viewer=None;manager.direct=False;manager.cleanup();manager.lock.close()
            reopened=Manager(temp,"/unused",FakeHypr())
            try:self.assertEqual(len(reopened.setups()["items"]),2)
            finally:reopened.cleanup();reopened.lock.close()

    def test_ipc_correlates_background_and_foreground_replies(self):
        manager=Mock()
        manager.status.return_value={"viewing":False}
        manager.apply.side_effect=RuntimeError("invalid layout")
        requests='{"action":"status","requestId":7}\n{"action":"apply","requestId":8,"layout":{}}\n'
        output=io.StringIO()
        with patch("backend.sys.stdin",io.StringIO(requests)), patch("backend.sys.stdout",output):
            serve(manager)
        replies=[json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual([r["requestId"] for r in replies],[7,8])
        self.assertTrue(replies[0]["ok"])
        self.assertFalse(replies[1]["ok"])

    def test_validation(self):
        for change in (lambda x:x.update(fps=0),lambda x:x["monitors"][0].update(width=0),lambda x:x["monitors"][1].update(x=0),lambda x:x["monitors"][0].update(id='bad"name')):
            layout=default_layout();change(layout)
            with self.assertRaises(ValueError):validate(layout)
    def test_monitor_count_is_capped_at_sixteen(self):
        layout=default_layout();layout["monitors"]=[{"id":str(i),"width":640,"height":480,"x":(i%4)*664,"y":(i//4)*504} for i in range(16)]
        validate(layout)
        layout["monitors"].append({"id":"16","width":640,"height":480,"x":0,"y":20000})
        with self.assertRaisesRegex(ValueError,"16"):
            validate(layout)
    def test_spacing(self):
        for gap in (0,-1,True,1.5,float("nan"),8193):
            layout=default_layout();layout["spacing"]=gap
            with self.assertRaises(ValueError):add_gutters(layout)
        for gap in (1,24,200):
            layout=default_layout();layout["spacing"]=gap
            layout["monitors"][1]["x"]=500
            adjusted=add_gutters(layout)
            validate(adjusted)
            self.assertEqual(add_gutters(adjusted),adjusted)
            self.assertEqual(layout["monitors"][1]["x"],500)
        legacy=default_layout();del legacy["spacing"]
        for i,m in enumerate(legacy["monitors"]):m["x"]=i*1920
        upgraded=add_gutters(legacy)
        self.assertEqual(upgraded["spacing"],24)
        self.assertEqual(upgraded["monitors"][1]["x"],1944)
        # Invalid explicit layouts are rejected by validation, before Hyprland.
        with self.assertRaises(ValueError):validate(legacy)

    def test_curvature(self):
        for value in (-1, 101, float("nan"), float("inf"), "20", True):
            for scope in ("workspace", "monitor"):
                layout=default_layout()
                target=layout if scope=="workspace" else layout["monitors"][0]
                target["curvature"]=value
                with self.assertRaises(ValueError):validate(layout)
        with tempfile.TemporaryDirectory() as temp:
            manager=Manager(temp,"/unused",FakeHypr())
            try:
                layout=default_layout();layout["curvature"]=65
                layout["monitors"][1]["curvature"]=80
                manager.apply(layout)
                self.assertEqual(manager.load(),layout)
                # A presentation-only apply must not issue output or Lua changes.
                original=manager.runner
                def read_only(*args):
                    self.assertEqual(args,("-j","monitors"))
                    return original(*args)
                manager.runner=read_only
                layout["curvature"]=90
                manager.apply(layout)
                manager.runner=original
                lines=[line for line in (Path(temp)/"viewer.tsv").read_text().splitlines() if not line.startswith("#")]
                self.assertEqual([line.split()[5] for line in lines],["0","80","0"])
            finally:manager.cleanup();manager.lock.close()

    def test_glasses_presentation_targets_viture_and_all_panels(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr()
            fake.outputs["DP-1"]={"name":"DP-1", "description":"VITURE", "width":1920, "height":1080, "x":1920, "y":0, "scale":1}
            renderer=Path(temp)/"renderer"; renderer.touch()
            manager=Manager(temp,renderer,fake)
            process=Mock();process.poll.return_value=None
            try:
                with patch("backend.subprocess.Popen",return_value=process) as spawn, patch.object(manager.sdk,"connect") as connect:
                    message=manager.present(default_layout())
                    self.assertIn("3 desktops",message)
                    connect.assert_called_once()
                    args=spawn.call_args.args[0]
                    self.assertEqual(args[args.index("--display")+1],"DP-1")
                    self.assertIn("--pose-socket",args)
                    self.assertEqual(len([line for line in (Path(temp)/"viewer.tsv").read_text().splitlines() if not line.startswith("#")]),3)
                    manager.stop_viewer()
                    self.assertFalse(manager.owned)
            finally:
                manager.cleanup();manager.lock.close()

    def test_missing_glasses_does_not_create_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake)
            try:
                with self.assertRaisesRegex(RuntimeError,"active VITURE"):
                    manager.present(default_layout())
                self.assertEqual(list(fake.outputs),["eDP-1"])
            finally:manager.cleanup();manager.lock.close()

    def test_direct_failure_restores_handoff_and_stereo(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr()
            fake.outputs["DP-1"]={"name":"DP-1","description":"VITURE","width":1920,"height":1080,"x":1920,"y":0,"availableModes":["3840x1080@60.00Hz"]}
            manager=Manager(temp,"/unused",fake)
            manager.sdk=Mock();manager.sdk.process.poll.return_value=None
            manager.dedicated=Mock();manager.dedicated.start.side_effect=RuntimeError("Denied lease")
            fake.outputs["DP-1"].update(refreshRate=120,scale=1)
            fake.outputs["DP-1"]["availableModes"].append("1920x1080@120.00Hz")
            real_runner=manager.runner
            manager.runner=lambda *args: "ok" if args[0]=="eval" else real_runner(*args)
            try:
                with self.assertRaisesRegex(RuntimeError,"Denied lease"):
                    manager.start_dedicated()
                self.assertEqual([call.args for call in manager.sdk.stereo.call_args_list],[(True,),(False,)])
                manager.dedicated.stop.assert_called()
                self.assertFalse(manager.direct);self.assertFalse(manager.stereo_active)
            finally:manager.cleanup();manager.lock.close()

    def test_startup_error_survives_rollback_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            manager=Manager(temp,"/unused",FakeHypr())
            manager.monitors=Mock(return_value=[{"name":"DP-1","description":"VITURE","width":1920,"availableModes":["3840x1080@60.00Hz"]}])
            manager.sdk=Mock();manager.sdk.process.poll.return_value=None
            manager.dedicated=Mock();manager.dedicated.start.side_effect=RuntimeError("original lease failure")
            manager.stop_viewer=Mock(side_effect=[None,RuntimeError("rollback failure")])
            try:
                with self.assertRaisesRegex(RuntimeError,"original lease failure.*rollback failure"):
                    manager.start_dedicated()
                events=[json.loads(line) for line in (Path(temp)/"display-events.jsonl").read_text().splitlines()]
                self.assertEqual(events[-2]["stage"],"stereo-start-failed")
                self.assertIn("original lease failure",events[-2]["error"])
            finally:manager.lock.close()

    def test_restore_waits_for_mode_family_before_refresh_request(self):
        with tempfile.TemporaryDirectory() as temp:
            manager=Manager(temp,"/unused",FakeHypr())
            original={"name":"DP-1","width":1920,"height":1080,"x":1920,"y":0,"scale":1,"refreshRate":120}
            manager.original_output=original
            manager.stereo_active=True
            manager.sdk=Mock();manager.dedicated=Mock()
            count=0;phase="family";events=[]
            def monitors():
                nonlocal count
                count+=1
                if phase=="family" and count<3:return []
                return [{**original,"description":"CVT VITURE","availableModes":["1920x1080@60.00Hz" if phase=="family" else "1920x1080@120.00Hz"]}]
            def restore_rate():
                nonlocal phase
                self.assertGreaterEqual(count,3)
                phase="rate";events.append("rate")
            manager.monitors=monitors
            manager.sdk.restore_rate.side_effect=restore_rate
            manager.sdk.verify_restore.side_effect=lambda:events.append("verify")
            manager.runner=lambda *args:events.append("scanout") or "ok"
            try:
                with patch("backend.time.sleep"):
                    manager.stop_viewer()
                self.assertEqual(events,["rate","scanout","verify"])
                self.assertFalse(manager.stereo_active)
            finally:manager.lock.close()

    def test_preview_resize_is_rejected_before_outputs_change(self):
        from unittest.mock import Mock
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake)
            manager.apply(default_layout())
            original=copy.deepcopy(fake.outputs)
            manager.viewer=Mock();manager.viewer.poll.return_value=None
            changed=default_layout();changed["monitors"][0]["width"]=1280
            with self.assertRaisesRegex(RuntimeError,"Dedicated stereo supports live resizing"):
                manager.apply(changed)
            self.assertEqual(fake.outputs,original)
            manager.viewer=None;manager.cleanup();manager.lock.close()

    def test_live_apply_never_restarts_viewer_or_sdk(self):
        from unittest.mock import Mock
        with tempfile.TemporaryDirectory() as temp:
            manager=Manager(temp,"/unused",FakeHypr())
            manager.apply(default_layout())
            viewer=Mock();viewer.poll.return_value=None
            manager.viewer=viewer;manager.direct=True;manager.stereo_active=True
            manager.stop_viewer=Mock(side_effect=AssertionError("Must preserve session"))
            manager.start_dedicated=Mock(side_effect=AssertionError("Must preserve lease"))
            try:
                layout=default_layout();layout.update(curvature=70,spacing=40,fps=20)
                manager.apply(layout)
                layout["monitors"].pop()
                layout["monitors"][0]["width"]=1280
                manager.apply(layout)
                self.assertIs(manager.viewer,viewer)
                self.assertTrue(manager.direct)
                self.assertTrue(manager.stereo_active)
                viewer.terminate.assert_not_called()
                self.assertTrue((Path(temp)/"viewer.tsv").read_text().startswith("# settings 20 70 40"))
            finally:
                manager.remove(list(manager.owned));manager.lock.close()

    def test_lifecycle(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr(); manager=Manager(temp,"/unused",fake)
            try:
                layout=default_layout();manager.apply(layout)
                self.assertEqual(len(manager.owned),3)
                self.assertEqual(fake.outputs[manager.prefix+"1"]["x"],2020)
                changed=copy.deepcopy(layout);changed["monitors"]=changed["monitors"][:2];changed["monitors"][0]["height"]=720
                manager.apply(changed)
                self.assertEqual(len(manager.owned),2)
                self.assertEqual(manager.load(),changed)
                self.assertEqual(len([line for line in (Path(temp)/"viewer.tsv").read_text().splitlines() if not line.startswith("#")]),2)
                manager.cleanup();self.assertEqual(list(fake.outputs),["eDP-1"])
            finally:manager.lock.close()
    def test_layout_switch_without_leftover_physical_display(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr(); manager=Manager(temp,"/unused",fake)
            manager.graphics_limits={"maxWidth":8192,"maxHeight":8192}
            try:
                layout=default_layout();manager.apply(layout)
                origin=fake.outputs[manager.prefix+"1"]["x"]
                del fake.outputs["eDP-1"]
                # Setup switch with no leftover physical display and no laptop journal.
                manager.use_setup("builtin:two-fhd")
                self.assertEqual(len(manager.owned),2)
                self.assertEqual(fake.outputs[manager.prefix+"1"]["x"],origin)
                self.assertNotIn("eDP-1",fake.outputs)
                changed=copy.deepcopy(manager.load())
                for m in changed["monitors"]:m["id"]+="-b"
                manager.apply(changed)
                self.assertEqual(len(manager.owned),2)
                self.assertEqual(fake.outputs[manager.prefix+"1-b"]["x"],origin)
                fake.outputs.clear();manager.owned.clear();manager.applied=None
                with self.assertRaisesRegex(RuntimeError,"Keep at least one existing display"):
                    manager.apply(default_layout())
            finally:manager.lock.close()
    def test_failed_creation_is_cleaned(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake);fake.fail=True
            try:
                with self.assertRaises(RuntimeError):manager.apply(default_layout())
                self.assertFalse(manager.owned);self.assertEqual(list(fake.outputs),["eDP-1"])
            finally:manager.lock.close()
    def test_stale_outputs_recovery(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake);manager.apply(default_layout());manager.lock.close()
            recovered=Manager(temp,"/unused",fake)
            try:self.assertEqual(list(fake.outputs),["eDP-1"])
            finally:recovered.lock.close()

    def test_viewer_log_rotates_and_exit_code_is_reported(self):
        with tempfile.TemporaryDirectory() as temp:
            renderer=Path(temp)/"renderer"
            renderer.write_text("")
            manager=Manager(temp,renderer,FakeHypr())
            (Path(temp)/"viewer.log").write_text("previous crash\n")
            process=Mock();process.poll.return_value=None;process.pid=42
            try:
                manager.apply(default_layout())
                with patch("backend.subprocess.Popen",return_value=process):
                    manager.start()
                self.assertEqual((Path(temp)/"viewer.log.1").read_text(),"previous crash\n")
                self.assertEqual((Path(temp)/"viewer.log").read_text(),"")
                process.poll.return_value=9
                process.returncode=9
                status=manager.status()
                self.assertEqual(status["viewerExit"],"Viewer exited (code 9)")
                self.assertIn("Viewer exited (code 9)",status["restorationError"])
                self.assertIsNone(manager.viewer)
                self.assertIn("Viewer exited (code 9)",(Path(temp)/"backend.log").read_text())
            finally:
                manager.cleanup();manager.lock.close()

    def test_request_failure_survives_a_broken_status(self):
        with tempfile.TemporaryDirectory() as temp:
            manager=Manager(temp,"/unused",FakeHypr())
            try:
                manager.status=Mock(side_effect=KeyError("layout"))
                stdout=io.StringIO()
                with patch("sys.stdin",io.StringIO('{"requestId":7,"action":"not-a-real-action"}\n')), patch("sys.stdout",stdout):
                    serve(manager)
                reply=json.loads(stdout.getvalue().splitlines()[0])
                self.assertFalse(reply["ok"])
                self.assertEqual(reply["requestId"],7)
                self.assertIn("ValueError",reply["message"])
                self.assertIn("KeyError",reply["statusError"])
                log=(Path(temp)/"backend.log").read_text()
                self.assertIn("Traceback",log)
                self.assertIn("KeyError",log)
            finally:
                manager.lock.close()

    def test_stranded_side_by_side_mode_can_start_again(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr()
            fake.outputs["DP-1"]={"name":"DP-1","description":"VITURE","width":3840,"height":1080,"x":0,"y":0,"scale":1,"availableModes":["3840x1080@60.00Hz"]}
            manager=Manager(temp,"/unused",fake)
            saved={"name":"DP-1","width":1920,"height":1080,"refreshRate":120,"x":1920,"y":0,"scale":1}
            manager.stereo_active=True
            manager.original_output=saved
            manager.sdk=Mock()
            manager.sdk.process=None
            manager.sdk.state={"communication":False}
            def connect():
                manager.sdk.process=Mock()
                manager.sdk.process.poll.return_value=None
                manager.sdk.state={"communication":True}
            manager.sdk.connect.side_effect=connect
            manager.sdk.stereo.side_effect=lambda enabled: (_ for _ in ()).throw(RuntimeError("mode family missing")) if not enabled else None
            manager.dedicated=Mock()
            manager.start=Mock()
            try:
                with patch("backend.time.sleep"):
                    manager.start_dedicated()
            except Exception:
                pass
            manager.sdk.connect.assert_called()
            self.assertEqual(manager.original_output, saved)
            manager.lock.close()

    def test_stranded_stereo_is_restored_on_startup(self):
        with tempfile.TemporaryDirectory() as temp:
            first=Manager(temp,"/unused",FakeHypr())
            first.stereo_active=True
            first.original_output={"name":"DP-1","width":1920,"height":1080,"refreshRate":60,"x":0,"y":0,"scale":1}
            first.record_stereo()
            first.lock.close()
            with patch.object(SDK,"connect",return_value=None), patch.object(Manager,"stop_viewer") as stop:
                second=Manager(temp,"/unused",FakeHypr())
                stop.assert_called_once()
                self.assertTrue(second.stereo_active)
                second.lock.close()

    def test_startup_restore_error_is_not_repeated(self):
        with tempfile.TemporaryDirectory() as temp:
            first=Manager(temp,"/unused",FakeHypr())
            first.stereo_active=True
            first.original_output={"name":"DP-1","width":1920,"height":1080,"refreshRate":120,"x":0,"y":0,"scale":1}
            first.record_stereo()
            first.lock.close()
            def fail(self):
                self.restoration_error="The glasses were not asked to leave side-by-side mode"
                raise RuntimeError("XR display restoration needs retry: "+self.restoration_error)
            with patch.object(SDK,"connect",return_value=None), patch.object(Manager,"stop_viewer",fail):
                second=Manager(temp,"/unused",FakeHypr())
                self.assertEqual(second.restoration_error,"The glasses were not asked to leave side-by-side mode")
                second.lock.close()

    def test_bad_journal_keeps_the_worker_available(self):
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp)/"outputs.json").write_text("{")
            manager=Manager(temp,"/unused",FakeHypr())
            try:
                self.assertTrue(manager.restoration_error)
                self.assertTrue((Path(temp)/"outputs.json.corrupt").exists())
                self.assertFalse(manager.owned)
            finally:
                manager.lock.close()

    def test_invalid_layout_is_quarantined_on_load(self):
        with tempfile.TemporaryDirectory() as temp:
            manager=Manager(temp,"/unused",FakeHypr())
            (Path(temp)/"layout.json").write_text("{")
            try:
                stdout=io.StringIO()
                with patch("backend.sys.stdin", io.StringIO('{"requestId":1,"action":"load"}\n')), patch("backend.sys.stdout", stdout):
                    serve(manager)
                reply=json.loads(stdout.getvalue().splitlines()[0])
                self.assertTrue(reply["ok"])
                self.assertEqual(len(reply["layout"]["monitors"]), 3)
                self.assertIn("set aside", reply["message"])
                self.assertTrue((Path(temp)/"layout.json.corrupt").exists())
            finally:
                manager.lock.close()

if __name__=="__main__":unittest.main()
