"""Supervise the temporary privileged display handoff; rendering stays unprivileged."""
from pathlib import Path
import select
import subprocess
import time

HELPER = Path('/usr/lib/omarchy-xr/omarchy-xr-display')

class Dedicated:
    def __init__(self,directory,renderer):
        self.directory=Path(directory);self.renderer=str(renderer)
        self.process=None;self.log=None;self.output: str | None=None
    def start(self,output):
        if self.process:raise RuntimeError('The glasses are already being used for stereo.')
        if not HELPER.is_file():
            raise RuntimeError('Stereo helper missing. Open Utilities → Setup & integrations to install it')
        self.log=(self.directory/'dedicated.log').open('w')
        self.process=subprocess.Popen(['pkexec','--disable-internal-agent',str(HELPER),output],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True)
        try:
            stdout=self.process.stdout
            if stdout is None or not select.select([stdout],[],[],45)[0]:raise RuntimeError('Administrator approval took too long. Try again.')
            if stdout.readline().strip()!='ready':raise RuntimeError('The glasses display could not be prepared, or approval was cancelled. Try again.')
            for _ in range(50):
                names=subprocess.check_output([self.renderer,'--list-leases'],text=True,timeout=3).splitlines()
                if output in names:self.output=output;return
                time.sleep(.1)
            raise RuntimeError('The desktop could not release the glasses for stereo. Stop XR, then try again.')
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
                raise RuntimeError('The glasses display is still being restored. Wait a moment, then try again.')
            finally:
                if self.process.poll() is not None:
                    self.process=None;self.output=None
        if self.log:self.log.close();self.log=None
