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
import io

class FakeHypr:
    def __init__(self):
        self.outputs={"eDP-1":{"name":"eDP-1","width":1920,"height":1080,"x":0,"y":0,"scale":1}}
        self.fail=False
    def __call__(self,*args):
        if args==("-j","monitors"): return json.dumps(list(self.outputs.values()))
        if args[:2]==("output","create"):
            name=args[3]; self.outputs[name]={"name":name,"width":1920,"height":1080,"x":1920,"y":0,"scale":1};return "ok"
        if args[:2]==("output","remove"):
            del self.outputs[args[2]];return "ok"
        if args[0]=="eval":
            if self.fail: raise RuntimeError("Injected compositor failure")
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

    def test_hide_keeps_viewer_and_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake)
            manager.graphics_limits={"maxWidth":8192,"maxHeight":8192}
            try:
                manager.apply(default_layout())
                viewer=Mock();viewer.poll.return_value=None
                manager.viewer=viewer;manager.direct=True;manager.stereo_active=True
                owned=set(manager.owned)
                output=io.StringIO()
                requests="\n".join(json.dumps({"action":action}) for action in ("hide","status"))
                with patch("sys.stdin",io.StringIO(requests)), patch("sys.stdout",output):
                    serve(manager)
                replies=[json.loads(line) for line in output.getvalue().splitlines()]
                self.assertTrue(all(reply["ok"] for reply in replies))
                self.assertIn("stay running", replies[0]["message"])
                self.assertEqual(manager.owned, owned)
                self.assertIs(manager.viewer, viewer)
                self.assertTrue(manager.direct)
                self.assertTrue(manager.stereo_active)
                viewer.terminate.assert_not_called()
            finally:
                manager.viewer=None;manager.direct=False;manager.stereo_active=False
                manager.cleanup();manager.lock.close()

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
                layout=default_layout()
                layout.update(curvature=72, spacing=30, fps=30)
                layout["monitors"][0]["curvature"]=49
                layout=add_gutters(layout)
                manager.save_setup("Coding / focus",layout)
                item=manager.setups()["items"][0]
                self.assertEqual(item["layout"],layout)
                # No outputs are created when selecting a setup before startup.
                manager.use_setup(item["id"])
                self.assertFalse(manager.owned)
                self.assertEqual(manager.load(),layout)
                manager.apply(layout)
                other=copy.deepcopy(layout)
                other["monitors"]=other["monitors"][:2]
                other["monitors"][0]["width"]=1280
                other["curvature"]=10
                manager.save_setup("Reading",other)
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
                for name in ("", "  ", "x"*81, "a\nb", None, "coding / FOCUS"):
                    with self.assertRaises(ValueError):manager.save_setup(name,layout)
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
    def test_no_three_monitor_limit(self):
        layout=default_layout();layout["monitors"]=[{"id":str(i),"width":640,"height":480,"x":(i%10)*664,"y":(i//10)*504} for i in range(100)]
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
                    self.assertEqual(len(manager.owned),3)
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
                return [{**original,"availableModes":["1920x1080@60.00Hz" if phase=="family" else "1920x1080@120.00Hz"]}]
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

if __name__=="__main__":unittest.main()
