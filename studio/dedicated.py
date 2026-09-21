"""Supervise the temporary privileged display handoff; rendering stays unprivileged."""
from pathlib import Path
import select
import subprocess
import time

HELPER = Path('/usr/local/libexec/omarchy-xr-display')

class Dedicated:
    def __init__(self,directory,renderer):
        self.directory=Path(directory);self.renderer=str(renderer)
        self.process=None;self.log=None;self.output=None
    def start(self,output):
        if self.process:raise RuntimeError('Dedicated display is already reserved')
        if not HELPER.is_file():
            raise RuntimeError('Install the stereo helper first: make install-helper (one-time administrator setup)')
        self.log=(self.directory/'dedicated.log').open('w')
        self.process=subprocess.Popen(['pkexec','--disable-internal-agent',str(HELPER),output],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True)
        try:
            if not select.select([self.process.stdout],[],[],45)[0]:raise RuntimeError('Display handoff authorization timed out')
            if self.process.stdout.readline().strip()!='ready':raise RuntimeError('Display handoff failed or was cancelled. See '+str(self.directory/'dedicated.log'))
            for _ in range(50):
                names=subprocess.check_output([self.renderer,'--list-leases'],text=True,timeout=3).splitlines()
                if output in names:self.output=output;return
                time.sleep(.1)
            raise RuntimeError('Hyprland did not offer the headset for leasing')
        except Exception:
            self.stop();raise
    def stop(self):
        if self.process:
            try:
                if self.process.stdin and not self.process.stdin.closed:
                    self.process.stdin.close()
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                # Closing stdin is the watchdog signal even if polkit has not completed.
                if self.process.stdin and not self.process.stdin.closed:self.process.stdin.close()
                raise RuntimeError('Display restoration is still pending; see dedicated.log')
            finally:
                if self.process.poll() is not None:
                    self.process=None;self.output=None
        if self.log:self.log.close();self.log=None
