"""Temporary internal-output disable with an independent, unprivileged watchdog."""
import fcntl
import json
import math
import os
from pathlib import Path
import re
import select
import signal
import subprocess
import sys
import time

from atomic_file import atomic_write

INTERNAL = re.compile(r'(?:eDP|LVDS|DSI)-[0-9]+')


def hypr(*args):
    reply = subprocess.run(['hyprctl', *args],capture_output=True,text=True,timeout=3)
    text = reply.stdout.strip()
    if reply.returncode or text.lower().startswith(('error','invalid','unknown')):
        raise RuntimeError(text or reply.stderr.strip() or 'Display command failed')
    return text


def internal(monitors):
    return [m for m in monitors if INTERNAL.fullmatch(m.get('name',''))
            and not m.get('disabled',False) and m.get('dpmsStatus',True) and m.get('width',0)>0]


def validate_snapshot(monitors):
    if not isinstance(monitors,list) or not monitors: raise ValueError('Invalid laptop display journal')
    for m in monitors:
        if not isinstance(m,dict) or not INTERNAL.fullmatch(m.get('name','')):
            raise ValueError('Only internal laptop displays can be restored')
        for key in ('width','height','x','y','scale','refreshRate','transform'):
            if type(m.get(key)) not in (float,int) or not math.isfinite(m[key]):
                raise ValueError('Invalid laptop display geometry')
        if not (0<m['width']<=32768 and 0<m['height']<=32768 and 0<m['scale']<=10 and 0<m['refreshRate']<=1000 and m['transform'] in range(8)):
            raise ValueError('Invalid laptop display mode')


def snapshot(monitors):
    result = [{key:m.get(key,0 if key=='transform' else 60 if key=='refreshRate' else 1 if key=='scale' else None)
               for key in ('name','width','height','x','y','scale','refreshRate','transform')} for m in internal(monitors)]
    if result: validate_snapshot(result)
    return result


def restore_command(m):
    mode=f'{m["width"]}x{m["height"]}@{m["refreshRate"]}'
    return ('hl.monitor({output='+json.dumps(m['name'])+', disabled=false, mode='+json.dumps(mode)+
            ', position='+json.dumps(f'{m["x"]}x{m["y"]}')+', scale='+str(m['scale'])+', transform='+str(m['transform'])+'})')


class LaptopDisplay:
    def __init__(self,directory,runner=None):
        self.directory=Path(directory)
        self.journal=self.directory/'laptop-display.json'
        self.runner=runner or hypr
        self.process=None
        self.log=None
        self.error=''

    def saved(self):
        if not self.journal.exists(): return []
        data=json.loads(self.journal.read_text())
        if data.get('session') != os.environ.get('HYPRLAND_INSTANCE_SIGNATURE',''):
            # Runtime monitor changes do not survive a compositor restart.
            self.journal.unlink();return []
        validate_snapshot(data.get('monitors'))
        return data['monitors']

    def restore(self):
        monitors=self.saved()
        for m in monitors: self.runner('eval',restore_command(m))
        if monitors:
            for _ in range(20):
                active={m['name'] for m in json.loads(self.runner('-j','monitors')) if not m.get('disabled',False)}
                if all(m['name'] in active for m in monitors): break
                time.sleep(.1)
            else: raise RuntimeError('Laptop display restoration needs retry')
        self.journal.unlink(missing_ok=True)
        self.error=''

    def recover(self):
        # A previous watchdog may still be restoring after a shell restart.
        request=self.directory/'laptop-display-restore'
        request.touch()
        with (self.directory/'laptop-display.lock').open('w') as lock:
            deadline=time.monotonic()+12
            while True:
                try: fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);break
                except BlockingIOError:
                    if time.monotonic()>deadline: raise RuntimeError('Laptop display restoration is still pending')
                    time.sleep(.1)
            self.restore()
            request.unlink(missing_ok=True)

    def start(self,renderer_pid,glasses_output):
        self.stop()
        if not internal(json.loads(self.runner('-j','monitors'))):
            raise RuntimeError('No active built-in laptop display')
        self.log=(self.directory/'laptop-display.log').open('w')
        self.process=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'watch',str(self.directory),str(renderer_pid),glasses_output],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True,start_new_session=True)
        try:
            stdout=self.process.stdout
            if stdout is None or not select.select([stdout],[],[],10)[0] or stdout.readline().strip()!='ready':
                raise RuntimeError('Could not disable laptop display; see laptop-display.log')
        except Exception:
            self.stop();raise

    def stop(self):
        if self.process:
            if self.process.stdin and not self.process.stdin.closed: self.process.stdin.close()
            try: self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                # Do not kill the process responsible for restoration.
                raise RuntimeError('Laptop display restoration is still pending')
            self.process=None
        if self.log: self.log.close();self.log=None
        self.recover()

    def status(self):
        try:
            monitors=self.saved()
            if self.process and self.process.poll() is not None:
                self.stop();monitors=self.saved()
            return {'off':bool(monitors),'outputs':[m['name'] for m in monitors],'error':self.error}
        except Exception as exc:
            self.error=str(exc)
            return {'off':self.journal.exists(),'outputs':[],'error':self.error}


def process_identity(pid):
    try:
        fields=Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()
        return None if fields[0]=='Z' else fields[19]
    except (OSError,IndexError): return None


def connection_alive(output):
    from glasses import detect
    if not detect([])['usb']: return False
    return any(p.read_text().strip()=='connected' for p in Path('/sys/class/drm').glob('card*-'+output+'/status'))


def watch(directory,pid,output):
    if not re.fullmatch(r'DP-[0-9]+',output): raise ValueError('Invalid glasses connector')
    display=LaptopDisplay(directory)
    stopped=False
    def stop(*_):
        nonlocal stopped
        stopped=True
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    with (Path(directory)/'laptop-display.lock').open('w') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        display.restore()
        identity=process_identity(pid)
        if not identity or not connection_alive(output): raise RuntimeError('Glasses are not connected')
        monitors=snapshot(json.loads(hypr('-j','monitors')))
        if not monitors: raise RuntimeError('No active laptop display')
        journal={'session':os.environ.get('HYPRLAND_INSTANCE_SIGNATURE',''),'monitors':monitors}
        atomic_write(display.journal, json.dumps(journal))
        try:
            # Parent death during launch must never cause a late blackout.
            if stopped or select.select([sys.stdin], [], [], 0)[0]:
                return
            disable_laptop(monitors)
            print('ready', flush=True)
            while not stopped and not (Path(directory) / "laptop-display-restore").exists() and process_identity(pid) == identity and connection_alive(output):
                if select.select([sys.stdin], [], [], .5)[0]:
                    break
        finally:
            restore_laptop(display)


def disable_laptop(monitors):
    for monitor in monitors:
        hypr('eval', 'hl.monitor({output=' + json.dumps(monitor['name']) + ', disabled=true})')
    for _ in range(20):
        active = {monitor['name'] for monitor in json.loads(hypr('-j', 'monitors'))}
        if all(monitor['name'] not in active for monitor in monitors):
            return
        time.sleep(.1)
    raise RuntimeError('Hyprland did not disable the laptop display')


def restore_laptop(display):
    # Retry independently of the app and leave a journal if restoration fails.
    for attempt in range(10):
        try:
            display.restore()
            return
        except Exception as exc:
            print(str(exc), file=sys.stderr, flush=True)
            if attempt == 9:
                raise
            time.sleep(.5)


if __name__=='__main__':
    try:
        if len(sys.argv)==5 and sys.argv[1]=='watch':watch(sys.argv[2],int(sys.argv[3]),sys.argv[4])
        elif len(sys.argv)==3 and sys.argv[1]=='restore':LaptopDisplay(sys.argv[2]).recover()
        else:raise ValueError('Usage: laptop_display.py restore STATE_DIRECTORY')
    except Exception as exc:
        print(str(exc),file=sys.stderr,flush=True);sys.exit(1)
