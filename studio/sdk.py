"""Nonblocking supervisor for the VITURE runtime shipped with the application."""
import json
import hashlib
import os
from pathlib import Path
from typing import Any
import subprocess
import sys
import time

from clock import boot_time
from glasses import read

PACKAGED_LIBRARY = Path("/usr/lib/omarchy-xr/sdk/libglasses.so")
PACKAGED_TERMS = Path("/usr/share/omarchy-xr/LICENSE")


class SDK:
    def __init__(self, directory, pose_socket=None):
        self.directory = Path(directory)
        self.state_file = self.directory / "sdk-status.json"
        self.pose_socket = Path(pose_socket) if pose_socket else self.directory / "pose.sock"
        self.process = None
        self.log = None
        self.started = 0.0
        self.pending = False
        self.pid = None
        self.state: dict[str, Any] = {"communication": False, "tracking": False, "message": "Head tracking disconnected.", "error": False}

    def library(self):
        configured = os.environ.get("VITURE_SDK_LIBRARY")
        if configured:
            return Path(configured).expanduser()
        packaged = PACKAGED_LIBRARY
        if packaged.is_file():
            return packaged
        return Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "omarchy-xr/sdk/libglasses.so"

    def license_accepted(self):
        if self.library() != PACKAGED_LIBRARY:
            return True  # A developer supplied their own SDK and its licence.
        try:
            terms = PACKAGED_TERMS.read_bytes()
            accepted = json.loads((self.directory / "license-acceptance.json").read_text())
            return accepted.get("sha256") == hashlib.sha256(terms).hexdigest()
        except (OSError, ValueError, AttributeError):
            return False

    def devices(self):
        return [int(read(p / "idProduct"), 16) for p in Path("/sys/bus/usb/devices").glob("*")
                if read(p / "idVendor").lower() == "35ca" and read(p / "idProduct")]

    def connect(self):
        if not self.library().is_file():
            raise RuntimeError("Head tracking software is missing. Open Setup & integrations and install the XR runtime.")
        if not self.license_accepted():
            raise RuntimeError("XR setup is not finished. Open Setup & integrations, choose Finish XR setup, and review and accept the terms.")
        devices = self.devices()
        if len(devices) != 1:
            raise RuntimeError("Connect one pair of VITURE glasses, then try again.")
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
        self.state = {"communication": False, "tracking": False, "message": "Head tracking disconnected.", "error": False}

    def restore(self):
        if self.pending or not self.process or self.process.poll() is not None or not self.state.get("communication"):
            raise RuntimeError("Connect head tracking and wait for it to become ready.")
        self.process.stdin.write("restore\n")
        self.process.stdin.flush()
        self.pending = True
        self.started = boot_time()
        self.state["message"] = "Restoring normal glasses video…"

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
            raise RuntimeError("Head tracking must be connected before stereo can start.")
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
        raise RuntimeError("The glasses took too long to change video mode. Reconnect them, then try again.")

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
                message = self.state.get("message")
                failure = message if isinstance(message, str) else "Head tracking stopped. Choose Connect tracking to try again."
            elif self.pid not in self.devices():
                failure = "Glasses disconnected. Reconnect the cable, then choose Connect tracking."
            else:
                heartbeat = self.state.get("heartbeat", 0)
                stamp = float(heartbeat) if isinstance(heartbeat, (int, float)) and not isinstance(heartbeat, bool) else 0.0
                if boot_time() - max(self.started, stamp) > 20:
                    failure = "Head tracking stopped responding. Choose Connect tracking to try again."
            if failure:
                self.disconnect()
                self.state.update(message=failure, error=True)
        library = self.library()
        return {"available": library.is_file(), "packaged": library == PACKAGED_LIBRARY,
                "licenseAccepted": self.license_accepted(), "busy": self.pending, **self.state}
