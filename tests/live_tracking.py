"""Opt-in Pro 2 integration test; requires glasses and no other SDK session."""
import json, subprocess, sys, tempfile, time
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'studio'))
from backend import Manager, default_layout
with tempfile.TemporaryDirectory(prefix='xr-tracking-') as temp:
    m=Manager(temp,root/'build/omarchy-xr')
    try:
        m.apply(default_layout())
        m.sdk.connect()
        for _ in range(150):
            status=m.sdk.status()
            if status.get('tracking'):break
            if status.get('error'):raise RuntimeError(status)
            time.sleep(.1)
        assert status.get('tracking'),status
        print('SDK:',status,flush=True)
        m.start(present=True)
        time.sleep(2)
        clients=json.loads(subprocess.check_output(['hyprctl','clients','-j']))
        viewer=next(c for c in clients if c['pid']==m.viewer.pid)
        glasses=next(o for o in m.monitors() if 'viture' in o.get('description','').lower())
        print('Viewer:',{k:viewer[k] for k in ('title','monitor','fullscreen','size')},flush=True)
        assert viewer['monitor']==glasses['id'], 'Viewer on wrong display'
        assert viewer['fullscreen']==2, 'Viewer not fullscreen'
        m.stop_viewer()
        log=(Path(temp)/'viewer.log').read_text()
        print(log)
        assert 'Head tracking: live' in log
    finally:
        m.sdk.disconnect();m.cleanup();m.lock.close()
