"""Opt-in stereo/DRM lease test. Requires Pro 2 and an available polkit agent."""
import json,subprocess,sys,tempfile,time
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'studio'))
from backend import Manager,default_layout
with tempfile.TemporaryDirectory(prefix='xr-direct-') as temp:
    m=Manager(temp,root/'build/omarchy-xr')
    try:
        m.apply(default_layout())
        m.start_dedicated()
        time.sleep(3)
        assert m.viewer.poll() is None, (Path(temp)/'viewer.log').read_text()
        clients=json.loads(subprocess.check_output(['hyprctl','clients','-j']))
        assert all(c['pid']!=m.viewer.pid for c in clients),'Direct renderer must not create a desktop window'
        assert all('viture' not in o.get('description','').lower() for o in m.monitors()),'Leased glasses must not be a desktop output'
        assert m.sdk.status()['displayMode']==0x32
        m.camera_control('recenter');m.camera_control('fit')
        renderer=m.viewer
        m.stop_viewer()
        assert renderer.returncode == 0, f"Renderer failed during close: {renderer.returncode}"
        log=(Path(temp)/'viewer.log').read_text();print(log,flush=True)
        assert '3840x1080' in log and 'Head tracking: live' in log
        assert 'timed out' not in log and 'Head pose samples:' in log
        for _ in range(100):
            monitors=m.monitors()
            if any('viture' in o.get('description','').lower() and o['width']==1920 and any(v.startswith("1920x1080") for v in o.get("availableModes",[])) and not any(v.startswith("3840") for v in o.get("availableModes",[])) for o in monitors):break
            time.sleep(.1)
        else:raise AssertionError('Original desktop display was not restored')
        assert not (Path(temp)/'display-mode.json').exists()
        print('Stereo, live tracking, no desktop window/output, controls and restoration passed',flush=True)
    finally:
        try:m.cleanup()
        finally:m.sdk.disconnect();m.lock.close()
