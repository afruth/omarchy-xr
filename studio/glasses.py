"""Read-only glasses detection and explicitly requested USB-C recovery."""
from pathlib import Path
import os
import re
import shutil
import signal
import subprocess
import time

DRIVER = Path("/sys/bus/platform/drivers/ucsi_acpi")

# Fixed script, with the validated device passed as an argument, never shell code.
# The exit trap retries binding if the first bind fails or the shell is interrupted.
# Bash traps do not see the script's positional parameters, so the name is saved first.
RESET_SCRIPT = """
set -eu
cd /sys/bus/platform/drivers/ucsi_acpi
case "$1" in USBC[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]) ;; *) exit 2;; esac
test -L "$1"
device="$1"
rebind() { printf "%s" "$device" > bind 2>/dev/null || true; }
trap rebind EXIT
trap 'rebind; exit 143' TERM
printf "%s" "$1" > unbind
printf "%s" "$1" > bind
trap - EXIT
"""


def worker_pid(pid):
    try:
        comm = Path(f"/proc/{pid}/comm").read_text().strip()
    except OSError:
        return None
    if comm in ("sh", "bash", "dash"):
        return pid
    try:
        children = Path(f"/proc/{pid}/task/{pid}/children").read_text().split()
    except OSError:
        return None
    for token in children:
        try:
            if Path(f"/proc/{token}/comm").read_text().strip() in ("sh", "bash", "dash"):
                return int(token)
        except OSError:
            continue
    return None


def read(path):
    try:
        return path.read_text().strip()
    except OSError:
        return ""


def controllers(driver=DRIVER):
    return sorted(p.name for p in driver.glob("USBC*")
                  if p.is_symlink() and re.fullmatch(r"USBC[0-9A-Fa-f]{3}:[0-9A-Fa-f]{2}", p.name))


def detect(monitors, usb=Path("/sys/bus/usb/devices")):
    devices = [read(p / "product") or "VITURE glasses"
               for p in usb.glob("*") if read(p / "idVendor").lower() == "35ca"]
    displays = [m["name"] for m in monitors
                if not m.get("disabled", False) and m.get("width", 0) > 0
                and "viture" in " ".join(str(m.get(k, "")) for k in ("description", "make", "model")).lower()]
    return {"usb": bool(devices), "devices": devices, "displays": displays}


class Recovery:
    def __init__(self):
        self.process = None
        self.started = 0.0
        self.worker = None
        self.signaled = False
        self.message = ""

    def start(self):
        if self.process is not None:
            raise RuntimeError("USB-C recovery is already running")
        candidates = controllers()
        if len(candidates) != 1:
            raise RuntimeError("Automatic recovery requires exactly one supported USB-C controller")
        if not shutil.which("pkexec"):
            raise RuntimeError("pkexec is required for the administrator prompt")
        self.process = subprocess.Popen(
            ["pkexec", "/bin/sh", "-c", RESET_SCRIPT, "omarchy-xr-reset", candidates[0]],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.started = 0.0
        self.worker = None
        self.signaled = False
        self.message = "Authorize the administrator prompt; then wait for USB-C to reconnect."

    def status(self):
        self.note_worker()
        self.signal_timeout()
        if self.process is not None and self.signaled:
            self.finish_timeout()
        elif self.process is not None:
            self.finish_exit()
        return {"recovering": self.process is not None, "recoveryMessage": self.message,
                "canReset": self.process is None and len(controllers()) == 1 and bool(shutil.which("pkexec"))}

    def note_worker(self):
        if self.process is None or self.worker is not None:
            return
        self.worker = worker_pid(self.process.pid)
        if self.worker:
            self.started = time.monotonic()

    def signal_timeout(self):
        worker = self.worker
        if self.process is None or worker is None or not self.started or self.signaled:
            return
        if time.monotonic() - self.started <= 120:
            return
        try:
            os.kill(worker, signal.SIGTERM)
        except OSError:
            pass
        self.signaled = True

    def finish_timeout(self):
        process = self.process
        if process is None:
            return
        code = process.poll()
        if code is None:
            try:
                code = process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                code = None
        self.message = "USB-C recovery timed out. You can retry."
        if code is not None:
            self.process = None

    def finish_exit(self):
        process = self.process
        if process is None:
            return
        code = process.poll()
        if code is None:
            return
        self.process = None
        if code == 0:
            self.message = "USB-C reinitialized. Waiting for video; if absent, unplug and reconnect the glasses."
        elif code in (126, 127):
            self.message = "Recovery was cancelled or authorization failed. You can retry."
        else:
            self.message = "USB-C recovery failed. Unplug the glasses and reconnect; a full shutdown may be needed."
