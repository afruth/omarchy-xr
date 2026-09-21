"""Opt-in live capture/layout update test; requires Pro 2; reserves glasses once and verifies the lease survives updates."""
import copy
from pathlib import Path
import sys
import tempfile
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"studio"))
from backend import Manager,default_layout
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="xr-layout-") as temp:
    m=Manager(temp,root/"build/omarchy-xr")
    try:
        layout=default_layout()
        m.apply(layout);m.start_dedicated()
        process=m.viewer
        sdk_process=m.sdk.process;lease_process=m.dedicated.process
        def update(config, expected):
            m.apply(config)
            deadline=time.monotonic()+5
            while time.monotonic()<deadline:
                assert process.poll() is None, (Path(temp)/"viewer.log").read_text()
                log=(Path(temp)/"viewer.log").read_text()
                if expected in log:break
                time.sleep(.05)
            else:raise AssertionError(log)
            assert m.viewer is process
            assert m.sdk.process is sdk_process
            assert m.dedicated.process is lease_process
        changed=copy.deepcopy(layout);changed.update(curvature=65,spacing=50,fps=25)
        changed["monitors"][0]["curvature"]=40
        update(changed,"Live layout applied: 3 panels, 25 fps")
        changed["monitors"].append(dict(id="four",x=0,y=1200,width=800,height=600))
        update(changed,"Live layout applied: 4 panels, 25 fps")
        changed["monitors"]=changed["monitors"][:2]
        changed["monitors"][0]["width"]=1280
        changed["fps"]=24
        update(changed,"Live layout applied: 2 panels, 24 fps")
        time.sleep(.75)  # allow a new-size screencopy frame to arrive
        m.stop_viewer()
        log=(Path(temp)/"viewer.log").read_text()
        print(log,flush=True)
        assert process.returncode==0, f"Renderer exit {process.returncode}"
        assert "deferred" not in log,log
        assert "(1280x1080)" in log, log
        print("Live curvature, spacing, capture rate, add/remove/resize: same renderer process")
    finally:
        try:m.cleanup()
        finally:m.sdk.disconnect();m.lock.close()
