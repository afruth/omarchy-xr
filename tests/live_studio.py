"""Opt-in real Hyprland integration test. Creates only uniquely named test outputs."""
import subprocess
import tempfile
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"studio"))
from backend import Manager

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="omarchy-xr-test-") as temp:
    manager=Manager(temp,root/"build/omarchy-xr")
    try:
        layout={"version":1,"fps":15,"monitors":[
            {"id":"a","width":1280,"height":720,"x":0,"y":0},
            {"id":"b","width":800,"height":1280,"x":1280,"y":0},
            {"id":"c","width":1024,"height":768,"x":0,"y":720},
            {"id":"d","width":640,"height":480,"x":2080,"y":0}]}
        manager.apply(layout)
        subprocess.run([manager.renderer,"--layout",str(Path(temp)/"viewer.tsv"),"--fps","15","--smoke-test"],check=True,timeout=25)
        layout["monitors"]=layout["monitors"][:2]
        layout["monitors"][0]["width"]=960
        layout["monitors"][1]["x"]=960
        manager.apply(layout)
        assert len(manager.owned)==2
        subprocess.run([manager.renderer,"--layout",str(Path(temp)/"viewer.tsv"),"--smoke-test"],check=True,timeout=25)
    finally:
        manager.cleanup()
        manager.lock.close()
print("Mixed resolutions, portrait panel, resize, removal, and cleanup passed")
