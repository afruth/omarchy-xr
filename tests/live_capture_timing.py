"""Opt-in capture cadence probe; moves the real cursor temporarily, then restores it.
Build build/capture-timing first (see docs/architecture.md).
"""
import subprocess,json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
monitors=json.loads(subprocess.check_output(['hyprctl','monitors','-j']))
m=next(m for m in monitors if m['name'].startswith('OMXR-'))
old=json.loads(subprocess.check_output(['hyprctl','cursorpos','-j']))
w=next(m['activeWorkspace']['id'] for m in monitors if m['focused'])
script=f'''local n=0; omarchy_xr_probe=hl.timer(function() n=n+1; if n>2000 then omarchy_xr_probe:set_enabled(false); return end; hl.dispatch(hl.dsp.cursor.move({{x={m['x']+400}+150*math.sin(n*.025),y={m['y']+500}}})) end, {{timeout=8,type="repeat"}})'''
try:
    subprocess.run(['hyprctl','eval',script],check=True,capture_output=True)
    results=[]
    for cursor,service in [('no-cursor','serviced'),('cursor','frame-only'),('cursor','serviced')]:
        r=subprocess.run([str(root/'build/capture-timing'),m['name'],cursor,service],capture_output=True,text=True,timeout=10,check=True)
        results.append(r.stdout.strip());print(r.stdout,flush=True)
    Path('/tmp/omarchy-xr-capture-timing.txt').write_text('\n'.join(results)+'\n')
finally:
    subprocess.run(['hyprctl','eval',f'''if omarchy_xr_probe then omarchy_xr_probe:set_enabled(false); omarchy_xr_probe=nil end; hl.dispatch(hl.dsp.focus({{workspace="{w}"}})); hl.dispatch(hl.dsp.cursor.move({{x={old['x']},y={old['y']}}}))'''],capture_output=True)
