"""Isolated VITURE C API session; no SDK binaries are distributed with this app."""
import ctypes as C
import json
import math
from pathlib import Path
import select
import sys
import time

POSE = C.CFUNCTYPE(None, C.POINTER(C.c_float), C.c_uint64)


class Session:
    def __init__(self, library):
        self.lib = C.CDLL(str(library))
        self.handle = None
        self.initialized = self.started = self.imu = False
        self.last_pose = 0
        self.samples = 0
        self.mode = None
        self.communication = False
        self.tracking_error = ""
        self.callback = POSE(self.on_pose)
        signatures = {
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
        if data and all(math.isfinite(data[i]) for i in range(7)):
            self.samples += 1
            self.last_pose = time.monotonic()

    def connect(self, pid):
        self.close()
        if not self.call("is_product_id_valid", pid):
            raise RuntimeError("This SDK does not support the connected product; install the current SDK")
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
            self.mode = self.check("get_display_mode", self.handle)
            self.communication = True
            try:
                self.check("open_imu", self.handle, 1, 2)  # pose, 120 Hz
                self.imu = True
            except RuntimeError as exc:
                self.tracking_error = str(exc)
        except Exception:
            self.close()
            raise

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

    def close(self):
        if self.handle:
            try:
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
        self.last_pose = self.samples = 0
        self.mode = None
        self.tracking_error = ""

    def state(self):
        return {"communication": self.communication,
                "tracking": bool(self.last_pose and time.monotonic() - self.last_pose < 2),
                "samples": self.samples, "displayMode": self.mode, "trackingError": self.tracking_error}


def main():
    library, target, pid = sys.argv[1:]
    target = Path(target)
    session = None
    message, error, sequence = "Connecting to glasses…", False, 0
    def publish():
        data = {"heartbeat": time.time(), "message": message, "error": error, "sequence": sequence,
                **(session.state() if session else {"communication": False, "tracking": False})}
        temp = target.with_suffix(".tmp")
        temp.write_text(json.dumps(data))
        temp.replace(target)
    try:
        publish()
        session = Session(library)
        session.connect(int(pid))
        message = "SDK connected; waiting for tracking samples. Video is checked separately."
        sequence += 1
        last_query = time.monotonic()
        while True:
            if time.monotonic() - last_query >= 5:
                session.mode = session.check("get_display_mode", session.handle)
                last_query = time.monotonic()
            publish()
            readable, _, _ = select.select([sys.stdin], [], [], 1)
            if readable:
                command = sys.stdin.readline()
                if not command or command.strip() == "disconnect":
                    break
                try:
                    if command.strip() != "restore":
                        raise ValueError("Unknown SDK command")
                    session.restore_display()
                    message, error = "Display mode reapplied and verified. Check video status separately.", False
                except Exception as exc:
                    message, error = str(exc), True
                sequence += 1
    except Exception as exc:
        message, error = str(exc), True
        sequence += 1
        if session:
            session.communication = False
            session.last_pose = 0
        publish()
    finally:
        if session:
            session.close()


if __name__ == "__main__":
    main()
