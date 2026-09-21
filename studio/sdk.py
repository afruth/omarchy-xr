"""Nonblocking supervisor for the optional, locally installed VITURE SDK."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from clock import boot_time
from glasses import read


class SDK:
    def __init__(self, directory, pose_socket=None):
        self.directory = Path(directory)
        self.state_file = self.directory / "sdk-status.json"
        self.pose_socket = Path(pose_socket) if pose_socket else self.directory / "pose.sock"
        self.process = None
        self.log = None
        self.started = 0
        self.pending = False
        self.pid = None
        self.state = {"communication": False, "tracking": False, "message": "SDK disconnected", "error": False}

    def library(self):
        configured = os.environ.get("VITURE_SDK_LIBRARY")
        if configured:
            return Path(configured).expanduser()
        return Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "omarchy-xr/sdk/libglasses.so"

    def devices(self):
        return [int(read(p / "idProduct"), 16) for p in Path("/sys/bus/usb/devices").glob("*")
                if read(p / "idVendor").lower() == "35ca" and read(p / "idProduct")]

    def connect(self):
        if not self.library().is_file():
            raise RuntimeError("VITURE SDK missing. Install the Linux x86_64 SDK with scripts/install-sdk.py")
        devices = self.devices()
        if len(devices) != 1:
            raise RuntimeError("Connect exactly one pair of VITURE glasses")
        self.disconnect()
        self.state_file.unlink(missing_ok=True)
        self.pid = devices[0]
        self.log = (self.directory / "sdk.log").open("w")
        self.process = subprocess.Popen(
            [sys.executable, "-B", str(Path(__file__).with_name("sdk_worker.py")), str(self.library()),
             str(self.state_file), str(self.pid), str(self.pose_socket)], stdin=subprocess.PIPE, stdout=self.log, stderr=self.log, text=True,
            env={**os.environ, "LD_LIBRARY_PATH": str(self.library().parent) + os.pathsep + os.environ.get("LD_LIBRARY_PATH", "")})
        self.started = boot_time()
        self.state = {"communication": False, "tracking": False, "message": "Connecting to glasses…", "error": False}
        self.pending = True

    def disconnect(self):
        if self.process:
            if self.process.poll() is None:
                try:
                    self.process.communicate("disconnect\n", timeout=2)
                except (subprocess.TimeoutExpired, BrokenPipeError):
                    self.process.kill()
                    try:
                        self.process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        pass
            self.process = None
        if self.log:
            self.log.close()
            self.log = None
        self.pending = False
        self.state = {"communication": False, "tracking": False, "message": "SDK disconnected", "error": False}

    def restore(self):
        if self.pending or not self.process or self.process.poll() is not None or not self.state.get("communication"):
            raise RuntimeError("Connect the SDK and wait for it to respond first")
        self.process.stdin.write("restore\n")
        self.process.stdin.flush()
        self.pending = True
        self.started = boot_time()
        self.state["message"] = "Reapplying the glasses display mode…"

    def stereo(self, enabled):
        self.display_command("stereo" if enabled else "stereo_off")

    def restore_rate(self):
        self.display_command("restore_rate")

    def verify_restore(self):
        self.display_command("verify_restore")

    def verify_stereo(self):
        self.display_command("verify_stereo")

    def display_command(self, command):
        deadline = time.monotonic() + 20
        while self.pending and time.monotonic() < deadline:
            self.status()
            time.sleep(.05)
        if not self.process or self.process.poll() is not None or not self.state.get("communication"):
            raise RuntimeError("SDK communication is required for stereo")
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        self.started = boot_time()
        self.pending = True
        while time.monotonic() < deadline:
            state = self.status()
            if not state["busy"]:
                if state.get("error"):
                    raise RuntimeError(state["message"])
                return
            time.sleep(.05)
        raise RuntimeError("Stereo display-mode command timed out")

    def status(self):
        if self.process:
            previous_sequence = self.state.get("sequence", -1)
            try:
                data = json.loads(self.state_file.read_text())
                if data.get("heartbeat", 0) >= self.started:
                    self.state = data
                    if data.get("sequence", 0) > previous_sequence and data.get("sequence", 0) > 0:
                        self.pending = False
            except (OSError, ValueError):
                pass
            failure = ""
            if self.process.poll() is not None:
                failure = self.state.get("message") if self.state.get("error") else "SDK process stopped. Connect again to retry."
            elif self.pid not in self.devices():
                failure = "Glasses unplugged. Reconnect the cable, then choose Connect glasses."
            elif boot_time() - max(self.started, self.state.get("heartbeat", 0)) > 20:
                failure = "SDK timed out. Connect again to retry."
            if failure:
                self.disconnect()
                self.state.update(message=failure, error=True)
        return {"available": self.library().is_file(), "busy": self.pending, **self.state}
