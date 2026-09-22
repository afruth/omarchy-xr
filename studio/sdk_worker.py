"""Isolated VITURE C API session; releases include the licensed Gen1/Gen2 runtime."""
import ctypes as C
import json
import math
from pathlib import Path
from typing import Any
import select
import socket
import threading
import sys
import time

from atomic_file import atomic_write
from clock import boot_time

POSE = C.CFUNCTYPE(None, C.POINTER(C.c_float), C.c_uint64)


class Session:
    def __init__(self, library, mode_journal=None):
        self.mode_journal = Path(mode_journal) if mode_journal else None
        self.original_mode = None
        self.lib = C.CDLL(str(library))
        self.handle = None
        self.initialized = self.started = self.imu = False
        self.pose_lock = threading.Lock()
        self.pose = None
        self.last_pose = 0.0
        self.samples = 0
        self.mode = None
        self.communication = False
        self.native_dof = None
        self.tracking_error = ""
        self.display_error = ""
        self.callback = POSE(self.on_pose)
        signatures = {
            "is_product_support_native_dof": ([C.c_int], C.c_int),
            "is_product_id_valid": ([C.c_int], C.c_int),
            "create": ([C.c_int], C.c_void_p),
            "get_device_type": ([C.c_void_p], C.c_int),
            "initialize": ([C.c_void_p, C.c_char_p, C.c_char_p], C.c_int),
            "start": ([C.c_void_p], C.c_int),
            "stop": ([C.c_void_p], C.c_int),
            "shutdown": ([C.c_void_p], C.c_int),
            "destroy": ([C.c_void_p], None),
            "register_imu_pose_callback": ([C.c_void_p, POSE], C.c_int),
            "open_imu": ([C.c_void_p, C.c_uint8, C.c_uint8], C.c_int),
            "close_imu": ([C.c_void_p, C.c_uint8], C.c_int),
            "get_display_mode": ([C.c_void_p], C.c_int),
            "get_brightness_level": ([C.c_void_p], C.c_int),
            "set_display_mode": ([C.c_void_p, C.c_int], C.c_int),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.lib, "xr_device_provider_" + name)
            fn.argtypes, fn.restype = args, result

    def call(self, name, *args):
        return getattr(self.lib, "xr_device_provider_" + name)(*args)

    def check(self, name, *args):
        code = self.call(name, *args)
        if code < 0:
            errors = {-2: "USB inaccessible; check cable and VITURE udev permissions",
                      -3: "USB transfer failed", -4: "Unsupported by this device",
                      -5: "Device did not respond", -7: "Device rejected command"}
            raise RuntimeError(f"{name}: {errors.get(code, 'SDK error')} ({code})")
        return code

    def on_pose(self, data, timestamp):
        if not data:
            return
        euler = tuple(float(data[i]) for i in range(3))
        if not all(math.isfinite(v) for v in euler):
            return
        now = time.monotonic()
        with self.pose_lock:
            self.pose = (now, *euler, int(timestamp))
            self.samples += 1
            self.last_pose = now
        notify = getattr(self, "pose_notify", None)
        if notify:
            notify()

    def pose_packet(self):
        with self.pose_lock:
            pose = self.pose
        if not pose or time.monotonic() - pose[0] > .25:
            return None
        mono, roll, pitch, yaw, device = pose
        return (f"euler-nwu-v2 {mono:.17g} {roll:.17g} {pitch:.17g} {yaw:.17g} {device}").encode("ascii")

    def connect(self, pid):
        self.close()
        if not self.call("is_product_id_valid", pid):
            raise RuntimeError("This SDK does not support the connected product; install the current SDK")
        capability = self.call("is_product_support_native_dof", pid)
        self.native_dof = bool(capability) if capability >= 0 else None
        self.handle = self.call("create", pid)
        if not self.handle:
            raise RuntimeError("SDK could not open glasses. Check USB connection and VITURE udev permissions")
        try:
            if self.call("get_device_type", self.handle) not in (0, 1):
                raise RuntimeError("This connection adapter currently supports Gen1/Gen2 glasses, including Pro 2")
            self.check("register_imu_pose_callback", self.handle, self.callback)
            self.check("initialize", self.handle, None, None)
            self.initialized = True
            self.check("start", self.handle)
            self.started = True
            # An acknowledged device query, not just a successfully allocated handle.
            self.check("get_brightness_level", self.handle)
            self.communication = True
            self.load_mode_journal()
            try:
                self.mode = self.check("get_display_mode", self.handle)
            except RuntimeError as exc:
                self.display_error = str(exc)
            try:
                self.check("open_imu", self.handle, 1, 2)  # pose, 120 Hz
                self.imu = True
            except RuntimeError as exc:
                self.tracking_error = str(exc)
        except Exception:
            self.close()
            raise

    def load_mode_journal(self):
        if not self.mode_journal or not self.mode_journal.exists():
            return
        original = json.loads(self.mode_journal.read_text())["mode"]
        if original not in (0x31, 0x32, 0x33, 0x34, 0x35, 0x41, 0x42, 0x43, 0x44, 0x45):
            raise RuntimeError("Invalid display recovery journal")
        self.original_mode = original
        # A reboot/replug may already have restored the saved mode.
        # Do not bounce a healthy 120-Hz link through 60 Hz on connect.
        try:
            if self.check("get_display_mode", self.handle) == original:
                self.verify_restore()
            else:
                # The supervisor must coordinate EDID/host timing changes.
                # Connecting tracking must not change display modes.
                self.display_error = "Previous display restoration is pending"
        except RuntimeError as exc:
            self.display_error = "Previous display restoration is pending: " + str(exc)

    def wait_mode(self, expected):
        deadline = time.monotonic() + 5
        actual = None
        while time.monotonic() < deadline:
            actual = self.check("get_display_mode", self.handle)
            if actual == expected:
                self.mode = actual
                return
            time.sleep(.1)
        raise RuntimeError(f"Display mode readback mismatch: expected {expected:#x}, got {actual!r}")

    def begin_stereo(self):
        if self.original_mode is None:
            self.original_mode = self.check("get_display_mode", self.handle)
            if self.mode_journal:
                atomic_write(self.mode_journal, json.dumps({"mode": self.original_mode}))
        self.check("set_display_mode", self.handle, 0x32)  # standard SBS 3840x1080, 60 Hz
        # Gen2 readback reports the active host video timing. Verify only after
        # the host starts the new 3840-wide scanout, not immediately after ACK.
        self.mode = self.check("get_display_mode", self.handle)

    def end_stereo(self):
        if self.original_mode is None:
            return
        # Request the 2D family first. The supervisor waits for hotplug/EDID
        # completion before requesting the original refresh rate. Sending these
        # back-to-back can interrupt DisplayPort negotiation on this laptop.
        family = 0x31 if self.original_mode in (0x31,0x33,0x34) else self.original_mode
        self.check("set_display_mode", self.handle, family)

    def restore_rate(self):
        if self.original_mode is not None and self.original_mode not in (0x31,0x32,0x41,0x42):
            self.check("set_display_mode", self.handle, self.original_mode)

    def verify_restore(self):
        if self.original_mode is None:
            return
        self.wait_mode(self.original_mode)
        self.original_mode = None
        if self.mode_journal:
            self.mode_journal.unlink(missing_ok=True)

    def restore_display(self):
        if not self.started:
            raise RuntimeError("Connect the SDK first")
        # Reapply the mode read from this device; do not guess supported modes.
        mode = self.check("get_display_mode", self.handle)
        self.check("set_display_mode", self.handle, mode)
        actual = self.check("get_display_mode", self.handle)
        if actual != mode:
            raise RuntimeError("Display mode readback did not match")
        self.mode = actual
        self.display_error = ""

    def close(self):
        if self.handle:
            try:
                # The manager restores mode while it can observe host hotplug.
                # Closing/reconnecting USB must never replay timed mode changes.
                # Keep the recovery journal if host restoration is still pending.
                if self.imu:
                    self.call("close_imu", self.handle, 1)
                if self.started:
                    self.call("stop", self.handle)
                if self.initialized:
                    self.call("shutdown", self.handle)
            finally:
                self.call("destroy", self.handle)
        self.handle = None
        self.initialized = self.started = self.imu = self.communication = False
        with self.pose_lock:
            self.pose = None
            self.last_pose = 0.0
            self.samples = 0
        self.mode = None
        self.tracking_error = ""
        self.display_error = ""

    def state(self):
        return {"communication": self.communication,
                "tracking": bool(self.last_pose and time.monotonic() - self.last_pose < 2),
                "samples": self.samples, "nativeDof": self.native_dof, "displayMode": self.mode, "trackingError": self.tracking_error, "displayError": self.display_error}


KEEP_ALIVE_LIMIT = 3


def record_keep_alive(failures, error):
    failures += 1
    if failures >= KEEP_ALIVE_LIMIT:
        raise RuntimeError(error)
    return failures


class PosePublisher:
    """Pose transport stays off the USB control calls. The callback wakes the sender; the thread only notices a stale pose."""
    def __init__(self, session, path):
        self.session, self.path = session, str(path)
        self.stop_event = threading.Event()
        self.wake = threading.Condition()
        self.generation = 0
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.socket.setblocking(False)
        session.pose_notify = self.notify
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def notify(self):
        with self.wake:
            self.generation += 1
            self.wake.notify()

    def run(self):
        previous = None
        seen = 0
        while not self.stop_event.is_set():
            packet = self.session.pose_packet()
            if packet and packet != previous:
                try:
                    self.socket.sendto(packet, self.path)
                    previous = packet
                except OSError:
                    self.stop_event.wait(0.25)
                continue
            with self.wake:
                if self.generation != seen:
                    seen = self.generation
                    continue
                self.wake.wait(timeout=0.25)
                seen = self.generation

    def close(self):
        self.stop_event.set()
        self.notify()
        self.thread.join(timeout=1)
        self.socket.close()


def sdk_command(session, action):
    if action == "stereo":
        session.begin_stereo()
        return "Stereo SBS requested; waiting for the host video mode."
    if action == "verify_stereo":
        session.wait_mode(0x32)
        return "Stereo video mode verified (3840×1080 at 60 Hz)."
    if action == "stereo_off":
        session.end_stereo()
        return "Previous display mode requested; waiting for host restoration."
    if action == "restore_rate":
        session.restore_rate()
        return "Previous refresh rate requested."
    if action == "verify_restore":
        session.verify_restore()
        return "Previous glasses display mode restored and verified."
    if action == "restore":
        session.restore_display()
        return "Display mode reapplied and verified. Check video status separately."
    raise ValueError("Unknown SDK command")


def keep_alive(session, failures, last_query):
    if time.monotonic() - last_query < 5:
        return failures, last_query
    try:
        session.check("get_brightness_level", session.handle)
        return 0, time.monotonic()
    except RuntimeError as exc:
        print(f"SDK keep-alive failed ({failures + 1}): {exc}", file=sys.stderr, flush=True)
        return record_keep_alive(failures, str(exc)), time.monotonic()


def accept_command(session, state):
    command = sys.stdin.readline()
    if not command or command.strip() == "disconnect":
        return False
    try:
        state["message"] = sdk_command(session, command.strip())
        state["error"] = False
    except Exception as exc:
        state["message"], state["error"] = str(exc), True
    state["sequence"] += 1
    return True


def serve_sdk(session, publish, state):
    last_query = time.monotonic()
    reported_tracking = False
    last_publish = 0.0
    failures = 0
    while True:
        if session.state()["tracking"] and not reported_tracking:
            state["message"] = "SDK connected and receiving head tracking. Video is checked separately."
            state["sequence"] += 1
            reported_tracking = True
        failures, last_query = keep_alive(session, failures, last_query)
        if time.monotonic() - last_publish >= 1:
            publish()
            last_publish = time.monotonic()
        readable, _, _ = select.select([sys.stdin], [], [], .1)
        if not readable:
            continue
        if not accept_command(session, state):
            return
        last_publish = 0.0


def main():
    library, status_name, pid, *rest = sys.argv[1:]
    target = Path(status_name)
    pose_path = Path(rest[0]) if rest else target.with_name("pose.sock")
    session = None
    publisher = None
    state: dict[str, Any] = {"message": "Connecting to glasses…", "error": False, "sequence": 0}
    def publish():
        data = {"heartbeat": boot_time(), "message": state["message"], "error": state["error"], "sequence": state["sequence"],
                **(session.state() if session else {"communication": False, "tracking": False})}
        atomic_write(target, json.dumps(data))
    try:
        publish()
        session = Session(library, target.with_name("display-mode.json"))
        session.connect(int(pid))
        publisher = PosePublisher(session, pose_path)
        state["message"] = "SDK connected; waiting for tracking samples. Video is checked separately."
        state["sequence"] += 1
        serve_sdk(session, publish, state)
    except Exception as exc:
        state["message"], state["error"] = str(exc), True
        state["sequence"] += 1
        if session:
            session.communication = False
            session.last_pose = 0
        publish()
    finally:
        if publisher:
            publisher.close()
        if session:
            session.close()


if __name__ == "__main__":
    main()
