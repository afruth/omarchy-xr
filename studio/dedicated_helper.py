#!/usr/bin/python3 -I
"""Temporary VITURE-only EDID handoff. Run via polkit; restore on stdin EOF.

No firmware files, boot settings or permanent privileged service are installed.
"""
from pathlib import Path
import fcntl
import os
import re
import select
import signal
import sys
import time


OUR_DISPLAYID_HEADER = bytes([0x70, 0x20, 7, 8, 0, 0x7e, 0, 4, 0x92, 0x02, 0x3a, 0])


def own_override(data):
    return len(data) >= 256 and data[-128:-116] == OUR_DISPLAYID_HEADER


def headset_edid(data):
    if len(data) < 128 or len(data) % 128 or data[:8] != bytes.fromhex('00ffffffffffff00'):
        raise ValueError('Invalid EDID')
    if data[126]+1 != len(data)//128 or any(sum(data[i:i+128]) % 256 for i in range(0,len(data),128)):
        raise ValueError('Invalid EDID size or checksum')
    names=[data[i+5:i+18].decode('ascii',errors='ignore').strip() for i in range(54,126,18) if data[i:i+5]==b'\x00\x00\x00\xfc\x00']
    if not any('VITURE' in name.upper() for name in names):
        raise ValueError('Only VITURE displays may be handed off')
    if data[126] >= 254:
        raise ValueError('Too many EDID extensions')
    # Preserve timings and identity; append DisplayID 2.0, primary use AR (8).
    # Include a VESA vendor block so older kernels visit the section header.
    ext=bytearray(128)
    ext[:12]=bytes([0x70,0x20,7,8,0,0x7e,0,4,0x92,0x02,0x3a,0])
    ext[12]=(-sum(ext[1:12]))%256
    ext[127]=(-sum(ext[:127]))%256
    base=bytearray(data);base[126]+=1;base[127]=(-sum(base[:127]))%256
    return bytes(base+ext)


def claim_connector(name):
    candidates = [path for path in Path('/sys/class/drm').glob('card*-' + name) if (path / 'status').read_text().strip() == 'connected']
    if len(candidates) != 1:
        raise RuntimeError('Expected one connected matching display')
    connector = candidates[0]
    edid = headset_edid((connector / 'edid').read_bytes())
    card = connector.name.split('-')[0]
    minor = (Path('/sys/class/drm') / card / 'dev').read_text().strip().split(':')[1]
    override = Path('/sys/kernel/debug/dri') / minor / name / 'edid_override'
    if not override.is_file():
        raise RuntimeError('Kernel EDID override interface is unavailable')
    return connector, edid, override


def clear_own_override(override):
    # Refuse to discard someone else's existing override.
    previous = override.read_bytes()
    if previous.strip() not in (b'', b'unset') and not own_override(previous):
        raise RuntimeError('An EDID override already exists; leaving it untouched')
    if own_override(previous):
        override.write_text('reset')


def card_directory(connector):
    return connector.parent / connector.name.split('-')[0]


def uses_amdgpu(connector):
    return (card_directory(connector) / 'device/driver').resolve().name == 'amdgpu'


def hotplug_event(connector):
    # amdgpu sends no uevent for a forced sysfs status, so compositors would not re-probe.
    (card_directory(connector) / 'uevent').write_text('change')


def reprobe_steps(connector, override, write_override):
    """Apply (hand over) or reset (restore) the override and make the compositor re-read the display."""
    status = connector / 'status'
    if not uses_amdgpu(connector):
        # The original sequence, unchanged for i915 and other drivers.
        return (
            ('disable', lambda: status.write_text('off')),
            ('wait', lambda: time.sleep(.3)),
            ('override', write_override),
            ('detect', lambda: status.write_text('detect')),
        )
    # amdgpu keeps a cached EDID that it re-applies on every probe and refreshes only in its
    # force() hook, where the EDID read returns nothing while the connector is forced off.
    # Forcing "on" before "detect" makes it cache the override (or, on restore, the real EDID).
    return (
        ('override', write_override),
        ('disable', lambda: status.write_text('off')),
        ('disable event', lambda: hotplug_event(connector)),
        ('wait', lambda: time.sleep(1)),
        ('refresh', lambda: status.write_text('on')),
        ('detect', lambda: status.write_text('detect')),
        ('detect event', lambda: hotplug_event(connector)),
    )


def hand_over(connector, edid, override):
    for _, action in reprobe_steps(connector, override, lambda: override.write_bytes(edid)):
        action()


def restore_connector(connector, override):
    for label, action in reprobe_steps(connector, override, lambda: override.write_text('reset')):
        try:
            action()
        except Exception as exc:
            print(f'restore {label}: {exc}', file=sys.stderr, flush=True)


def main():
    if os.geteuid() != 0:
        raise RuntimeError('Dedicated output handoff requires administrator authorization')
    if len(sys.argv) != 2 or not re.fullmatch(r'DP-[0-9]+', sys.argv[1]):
        raise ValueError('Expected one DisplayPort connector name')
    connector, edid, override = claim_connector(sys.argv[1])
    lock = Path('/run/omarchy-xr-display.lock').open('w')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    clear_own_override(override)
    shutting_down=False
    def stop(*_):
        nonlocal shutting_down
        if shutting_down: return
        shutting_down=True
        signal.signal(signal.SIGTERM,signal.SIG_IGN)
        signal.signal(signal.SIGINT,signal.SIG_IGN)
        signal.signal(signal.SIGHUP,signal.SIG_IGN)
        raise SystemExit(0)
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGHUP,stop)
    # A cancelled authorization may have outlived the manager. Do not touch video.
    if select.select([sys.stdin], [], [], 0)[0]:
        lock.close()
        return
    touched=False
    try:
        touched=True
        hand_over(connector, edid, override)
        print('ready',flush=True)
        # The owning manager retains this pipe. Crash/exit closes it automatically.
        for line in sys.stdin:
            if line.strip()=='stop':break
    finally:
        if touched:
            restore_connector(connector, override)
        lock.close()

if __name__=='__main__':
    try:main()
    except Exception as exc:
        print(str(exc),file=sys.stderr,flush=True);sys.exit(1)
