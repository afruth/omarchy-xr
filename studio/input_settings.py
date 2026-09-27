"""Validated, data-only XR control preferences (never executable Lua).

One XR key layer (docs/xr-controls-plan.md): a held modifier (default CTRL + ALT) plus one key per
action, the same in virtual monitors and Window canvas mode. config/xr-controls.lua reads the TSV
mailbox written here and binds only while the viewer runs.
"""
import json
from pathlib import Path
import re

from atomic_file import atomic_write

# (id, group, title, default key, scope). Scope "canvas": bound only in Window canvas mode.
# config/xr-controls.lua keeps the matching id -> action table; tests/test_input_settings.py checks it.
ACTIONS=(
    ('recenter','View','Recenter','space','both'),
    ('grab','View','Grab (hold to carry the view)','G','both'),
    ('zoom_in','View','Zoom in','equal','both'),
    ('zoom_out','View','Zoom out','minus','both'),
    ('overview','View','Overview / fit all','Up','both'),
    ('focus','View','Focus the window you look at','Down','both'),
    ('fill','View','Fill with the selected window','Return','both'),
    ('previous','Windows','Previous window','Left','both'),
    ('next','Windows','Next window','Right','both'),
    ('scroll_up','Windows','Scroll up','Page_Up','canvas'),
    ('scroll_down','Windows','Scroll down','Page_Down','canvas'),
    ('search','Windows','Search windows','slash','both'),
    ('nudge_left','Canvas arrangement','Nudge left','SHIFT + Left','canvas'),
    ('nudge_right','Canvas arrangement','Nudge right','SHIFT + Right','canvas'),
    ('nudge_up','Canvas arrangement','Nudge up','SHIFT + Up','canvas'),
    ('nudge_down','Canvas arrangement','Nudge down','SHIFT + Down','canvas'),
    ('narrower','Canvas arrangement','Narrower','comma','canvas'),
    ('wider','Canvas arrangement','Wider','period','canvas'),
    ('shorter','Canvas arrangement','Shorter','SHIFT + comma','canvas'),
    ('taller','Canvas arrangement','Taller','SHIFT + period','canvas'),
    ('pin','Canvas arrangement','Pin to view','P','canvas'),
    ('arrange','Canvas arrangement','Arrange','A','canvas'),
    ('undo','Canvas arrangement','Undo','Z','canvas'),
    ('redo','Canvas arrangement','Redo','SHIFT + Z','canvas'),
    ('notification_dismiss','Notifications & system','Dismiss notification','N','both'),
    ('notification_next','Notifications & system','Next notification','SHIFT + N','both'),
    ('pointer_home','Notifications & system','Pointer to laptop screen','Home','both'),
    ('help','Notifications & system','Show keys in the headset','H','both'),
)
ACTION_IDS=tuple(a[0] for a in ACTIONS)
DEFAULT_MODIFIER='CTRL + ALT'
DEFAULTS={'version':2,'modifier':DEFAULT_MODIFIER,'fingers':3,'keys':{a[0]:a[3] for a in ACTIONS}}
MODS={'SHIFT':1,'CTRL':4,'ALT':8,'SUPER':64}
_NAMED=('Up','Down','Left','Right','Home','End','Page_Up','Page_Down','Return','space','Tab','BackSpace','Insert',
        'Delete','minus','equal','comma','period','slash','semicolon','apostrophe','bracketleft','bracketright',
        'backslash','grave')
KEYS={k.lower():k for k in _NAMED}
# CTRL + ALT + F1…F12 switch virtual terminals in the kernel; never bind them.
VT_KEYS={f'f{n}' for n in range(1,13)}


def _mods(parts):
    mods=[p.strip().upper().replace('CONTROL','CTRL') for p in parts]
    if any(m not in MODS for m in mods) or len(set(mods))!=len(mods):
        raise ValueError('Use modifiers from SHIFT, CTRL, ALT and SUPER, each once')
    return [m for m in MODS if m in mods]


def modifier(value):
    """Normalized layer modifier and its Hyprland modmask."""
    if not isinstance(value,str) or not value.strip():raise ValueError('Choose a modifier for the XR key layer')
    mods=_mods(value.split('+'))
    if len(mods)<2 or not set(mods)&{'CTRL','ALT','SUPER'}:
        raise ValueError('The XR key layer needs two or more modifiers, such as CTRL + ALT')
    return ' + '.join(mods),sum(MODS[m] for m in mods)


def key(value):
    """Normalized action key ('' disables), its extra modmask (SHIFT) and the Hyprland key name."""
    if not isinstance(value,str):raise ValueError('Keys must be text')
    if not value.strip():return '',0,''
    parts=[p.strip() for p in value.split('+')]
    extra=_mods(parts[:-1])
    if extra not in ([],['SHIFT']):raise ValueError('An action key can add only SHIFT to the XR modifier')
    name=parts[-1]
    if name.lower() in KEYS:name=KEYS[name.lower()]
    elif re.fullmatch(r'[A-Za-z0-9]',name):name=name.upper()
    elif re.fullmatch(r'F([1-9]|[12][0-9]|3[0-5])',name.upper()):name=name.upper()
    else:raise ValueError('Use a letter, digit, F-key, navigation key or punctuation key')
    return ' + '.join(extra+[name]),MODS['SHIFT'] if extra else 0,name.lower()


def conflicts(value,bindings=()):
    """[(action, message)] for every key that cannot be bound; value is already normalized."""
    layer,mask=modifier(value['modifier'])
    found=[];seen={}
    for action in ACTION_IDS:
        text,extra,name=key(value['keys'].get(action,''))
        if not text:continue
        if extra and mask&MODS['SHIFT']:
            found.append((action,'The XR modifier already includes SHIFT'));continue
        full=(mask|extra,name)
        if full in seen:
            found.append((action,'Same key as '+title(seen[full])));continue
        seen[full]=action
        if mask&MODS['CTRL'] and mask&MODS['ALT'] and name in VT_KEYS:
            found.append((action,'CTRL + ALT + F1…F12 switch virtual terminals'));continue
        for b in bindings:
            if b.get('modmask')==full[0] and str(b.get('key','')).lower()==name and not str(b.get('description','')).startswith('XR:'):
                found.append((action,'Already used by '+(b.get('description') or 'another desktop binding')));break
    return found


def title(action):
    return next(a[2] for a in ACTIONS if a[0]==action)


def validate_controls(value,bindings=()):
    if not isinstance(value,dict) or not isinstance(value.get('keys'),dict):raise ValueError('Invalid control settings')
    fingers=value.get('fingers',3)
    if type(fingers) is not int or fingers not in (3,5):raise ValueError('Choose 3 or 5 fingers for zoom; 4 fingers are reserved for pan')
    unknown=set(value['keys'])-set(ACTION_IDS)
    if unknown:raise ValueError('Unknown XR action: '+sorted(unknown)[0])
    result={'version':2,'modifier':modifier(value.get('modifier',''))[0],'fingers':fingers,
            'keys':{a:key(value['keys'].get(a,''))[0] for a in ACTION_IDS}}
    problems=conflicts(result,bindings)
    if problems:
        action,message=problems[0]
        raise ValueError(f"{title(action)} ({chord(result,action)}): {message}")
    return result


def chord(value,action):
    """The full chord of an action, e.g. 'CTRL + ALT + SHIFT + Left' ('' when disabled)."""
    name=value['keys'].get(action,'')
    return value['modifier']+' + '+name if name else ''


def migrate(value):
    """Profiles before the XR layer (version 1: fingers plus five CTRL+arrow chords) load as the defaults."""
    if isinstance(value,dict) and value.get('version')!=2:
        fingers=value.get('fingers')
        return dict(DEFAULTS,keys=dict(DEFAULTS['keys']),fingers=3 if fingers==4 or fingers not in (3,5) else fingers)
    return value


def load_controls(directory):
    path=directory/'controls-settings.json'
    value=json.loads(path.read_text()) if path.exists() else DEFAULTS
    # Hyprland bindings are only checked on save; a profile stays loadable if Omarchy later claims a chord.
    return validate_controls(migrate(value))


def desktop_bindings(runner):
    """Non-XR Hyprland bindings as {modmask, key, description}, for the key map's conflict display."""
    return [{'modmask':b.get('modmask',0),'key':str(b.get('key','')),'description':b.get('description','')}
            for b in json.loads(runner('-j','binds')) if not str(b.get('description','')).startswith('XR:')]


def tsv(value):
    """Lua mailbox: 'v2', the modifier and fingers, then one 'key<TAB>action<TAB>key' line per action."""
    return ('v2\nmodifier\t'+value['modifier']+'\nfingers\t'+str(value['fingers'])+'\n'
            +''.join(f"key\t{a}\t{value['keys'][a]}\n" for a in ACTION_IDS))


def save_controls(directory,value,runner):
    value=validate_controls(value,json.loads(runner('-j','binds')))
    runner("eval", 'assert(omarchy_xr_controls and omarchy_xr_controls.refresh, "Open Utilities → Setup & integrations and install XR controls first")')
    # Atomic data mailbox, reread by the existing Lua timer. No compositor reload.
    path=directory/'controls-settings.tsv'
    profile=directory/'controls-settings.json'
    previous: dict[Path, bytes | None]={p:p.read_bytes() if p.exists() else None for p in (path,profile)}
    atomic_write(path, tsv(value))
    atomic_write(profile, json.dumps(value, indent=2)+'\n')
    try:
        runner('eval','omarchy_xr_controls.refresh()')
    except Exception:
        for item,saved in previous.items():
            if saved is None:item.unlink(missing_ok=True)
            else:atomic_write(item, saved.decode())
        raise
    return value
