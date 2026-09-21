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


def main():
    if os.geteuid()!=0:
        raise RuntimeError('Dedicated output handoff requires administrator authorization')
    if len(sys.argv)!=2 or not re.fullmatch(r'DP-[0-9]+',sys.argv[1]):
        raise ValueError('Expected one DisplayPort connector name')
    name=sys.argv[1]
    candidates=[p for p in Path('/sys/class/drm').glob('card*-'+name) if (p/'status').read_text().strip()=='connected']
    if len(candidates)!=1:raise RuntimeError('Expected one connected matching display')
    connector=candidates[0]
    edid=headset_edid((connector/'edid').read_bytes())
    card=connector.name.split('-')[0]
    minor=(Path('/sys/class/drm')/card/'dev').read_text().strip().split(':')[1]
    override=Path('/sys/kernel/debug/dri')/minor/name/'edid_override'
    if not override.is_file():raise RuntimeError('Kernel EDID override interface is unavailable')
    lock=Path('/run/omarchy-xr-display.lock').open('w')
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    # Refuse to discard someone else's existing override.
    previous=override.read_bytes()
    if previous.strip() not in (b'',b'unset'):
        raise RuntimeError('An EDID override already exists; leaving it untouched')
    status=connector/'status'
    def stop(*_):raise SystemExit(0)
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    # A cancelled authorization may have outlived the manager. Do not touch video.
    if select.select([sys.stdin], [], [], 0)[0]:
        lock.close()
        return
    touched=False
    try:
        touched=True
        status.write_text('off')
        time.sleep(.3)
        override.write_bytes(edid)
        status.write_text('detect')
        print('ready',flush=True)
        # The owning manager retains this pipe. Crash/exit closes it automatically.
        for line in sys.stdin:
            if line.strip()=='stop':break
    finally:
        if touched:
            status.write_text('off')
            time.sleep(.3)
            override.write_text('reset')
            status.write_text('detect')
        lock.close()

if __name__=='__main__':
    try:main()
    except Exception as exc:
        print(str(exc),file=sys.stderr,flush=True);sys.exit(1)
