"""Validated, data-only XR control preferences (never executable Lua)."""
import json
from pathlib import Path
import re

from atomic_file import atomic_write
ACTIONS=('fit_all','fit_target','recenter','zoom_in','zoom_out')
DEFAULTS={'fingers':3,'fit_all':'CTRL + Up','fit_target':'CTRL + Down','recenter':'','zoom_in':'','zoom_out':''}
MODS={'SHIFT':1,'CTRL':4,'ALT':8,'SUPER':64}
KEYS={k.lower():k for k in ('Up','Down','Left','Right','Home','End','Page_Up','Page_Down','Return','Tab','Escape','BackSpace','space','Insert','Delete')}
def chord(value):
    if not isinstance(value,str):raise ValueError('Hotkeys must be text')
    if not value.strip():return '',0,''
    parts=[p.strip() for p in value.split('+')]
    mods=[p.upper().replace('CONTROL','CTRL') for p in parts[:-1]]
    key=parts[-1]
    if not mods or any(m not in MODS for m in mods) or len(set(mods))!=len(mods):
        raise ValueError('Use modifiers such as CTRL + ALT + R, or leave blank to disable')
    if key.lower() in KEYS:key=KEYS[key.lower()]
    elif re.fullmatch(r'[A-Za-z0-9]',key):key=key.upper()
    elif re.fullmatch(r'F([1-9]|[12][0-9]|3[0-5])',key.upper()):key=key.upper()
    else:raise ValueError('Use a letter, digit, F1–F35, or a navigation key')
    mods=[m for m in MODS if m in mods]
    return ' + '.join(mods+[key]),sum(MODS[m] for m in mods),key.lower()
def validate_controls(value,bindings=()):
    if not isinstance(value,dict):raise ValueError('Invalid control settings')
    fingers=value.get('fingers')
    if type(fingers) is not int or fingers not in (3,5):raise ValueError('Choose 3 or 5 fingers for zoom; 4 fingers are reserved for pan')
    result={'fingers':fingers};seen=set()
    for action in ACTIONS:
        text,mask,key=chord(value.get(action,''));result[action]=text
        if not text:continue
        if (mask,key) in seen:raise ValueError('Each action needs a different hotkey')
        seen.add((mask,key))
        for b in bindings:
            if b.get('modmask')==mask and str(b.get('key','')).lower()==key and not str(b.get('description','')).startswith('XR:'):
                raise ValueError(f'{text} is already used by '+(b.get('description') or 'another desktop binding'))
    return result

def load_controls(directory):
    path=directory/'controls-settings.json'
    value=json.loads(path.read_text()) if path.exists() else dict(DEFAULTS)
    if value.get('fingers')==4:value['fingers']=3
    return validate_controls(value)

def save_controls(directory,value,runner):
    value=validate_controls(value,json.loads(runner('-j','binds')))
    runner("eval", 'assert(omarchy_xr_controls and omarchy_xr_controls.refresh, "Install XR controls with make install-controls first")')
    # Atomic data mailbox, reread by the existing Lua timer. No compositor reload.
    path=directory/'controls-settings.tsv'
    content=str(value['fingers'])+'\n'+''.join(value[k]+'\n' for k in ACTIONS)
    profile=directory/'controls-settings.json'
    previous: dict[Path, bytes | None]={p:p.read_bytes() if p.exists() else None for p in (path,profile)}
    atomic_write(path, content)
    atomic_write(profile, json.dumps(value, indent=2)+'\n')
    try:
        runner('eval','omarchy_xr_controls.refresh()')
    except Exception:
        for item,saved in previous.items():
            if saved is None:item.unlink(missing_ok=True)
            else:atomic_write(item, saved.decode())
        raise
    return value
