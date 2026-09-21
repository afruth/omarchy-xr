#!/usr/bin/env python3
"""Install optional Hyprland Lua input integration for the current user."""
from pathlib import Path
import os
import shutil
import time
import json
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
config=Path(os.environ.get('XDG_CONFIG_HOME',str(Path.home()/'.config')))/'hypr'
if not (config/'hyprland.lua').exists():
    raise SystemExit('Requires an Omarchy installation using Hyprland Lua')
# Do not shadow another custom compositor binding on a different installation.
existing=json.loads(subprocess.check_output(['hyprctl','binds','-j']))
sys.path.insert(0,str(root/'studio'))
from input_settings import load_controls, validate_controls
state=Path(os.environ.get('XDG_STATE_HOME',str(Path.home()/'.local/state')))/'omarchy-xr'
validate_controls(load_controls(state),existing)
bindings=config/'bindings.lua'
marker='require("hypr.xr-controls")'
text=bindings.read_text() if bindings.exists() else ''
if marker not in text:
    if bindings.exists():shutil.copy2(bindings,bindings.with_name('bindings.lua.before-xr-'+str(int(time.time()))))
    bindings.write_text(text+'\n-- XR controls are enabled only while the viewer is running.\n'+marker+'\n')
# Discover touchpads at installation time; never capture a separate mouse's button.
devices=json.loads(subprocess.check_output(['hyprctl','devices','-j']))
known={m['name'] for m in devices.get('mice',[])}
touchpads={name for name in known if 'touchpad' in name or 'trackpad' in name}
for event in Path('/sys/class/input').glob('event*'):
    props=subprocess.run(['udevadm','info','--query=property','--path',str(event.resolve())],capture_output=True,text=True)
    if 'ID_INPUT_TOUCHPAD=1' not in props.stdout:continue
    try: name=(event/'device/name').read_text().strip().lower().replace(' ','-')
    except OSError:continue
    if name in known:touchpads.add(name)
(config/'xr-touchpads.lua').write_text('-- Generated locally by Omarchy XR installation.\nreturn {'+', '.join(json.dumps(n,ensure_ascii=False) for n in sorted(touchpads))+'}\n')
print('Three-finger tap devices: '+(', '.join(sorted(touchpads)) or 'none detected'))
shutil.copy2(root/'config/xr-controls.lua',config/'xr-controls.lua')
print('Installed XR controls. Run hyprctl reload and hyprctl configerrors.')
