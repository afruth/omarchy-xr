import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "studio"))
from glasses import Recovery, controllers, detect, RESET_SCRIPT


BIND_HARNESS = r"""
set -eu
mount -t tmpfs tmpfs /sys/bus/platform/drivers/ucsi_acpi
d=/sys/bus/platform/drivers/ucsi_acpi
ln -s driver "$d/USBC000:00"
mkfifo "$d/bind"
: > "$d/unbind"
/bin/sh -c "$RESET" omarchy-xr-reset USBC000:00 >"$OUT/stdout" 2>"$OUT/stderr" &
pid=$!
i=0
while [ "$i" -lt 50 ]; do
  if [ -s "$d/unbind" ]; then break; fi
  i=$((i + 1))
  sleep 0.05
done
cp "$d/unbind" "$OUT/unbind"
set +e
kill -TERM "$pid"
i=0
delivered=0
while [ "$i" -lt 50 ]; do
  state=$(ps -o stat= -p "$pid" 2>/dev/null | tr -d " " || true)
  case "$state" in
    ""|Z*) break ;;
  esac
  pending=$(awk '/^(SigPnd|ShdPnd):/{printf "%s", $2}' "/proc/$pid/status" 2>/dev/null || true)
  case "$pending" in
    *[!0]*) ;;
    "") ;;
    *) delivered=1; break ;;
  esac
  i=$((i + 1))
  sleep 0.02
done
if kill -0 "$pid" 2>/dev/null; then
  state=$(ps -o stat= -p "$pid" | tr -d " ")
  case "$state" in
    Z*) ;;
    *)
      if [ "$delivered" -ne 1 ]; then
        echo "TERM was still pending" >&2
        kill -KILL "$pid" 2>/dev/null || true
        exit 1
      fi
      exec 3<>"$d/bind"
      ;;
  esac
fi
i=0
while [ "$i" -lt 30 ]; do
  state=$(ps -o stat= -p "$pid" 2>/dev/null | tr -d " " || true)
  case "$state" in
    ""|Z*) break ;;
  esac
  i=$((i + 1))
  sleep 0.1
done
if kill -0 "$pid" 2>/dev/null; then
  state=$(ps -o stat= -p "$pid" | tr -d " ")
  case "$state" in
    Z*) ;;
    *)
      echo "reset script did not exit" >&2
      kill -KILL "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      exit 1
      ;;
  esac
fi
wait "$pid"
printf '%s\n' "$?" > "$OUT/code"
if [ -e /dev/fd/3 ]; then
  dd if=/dev/fd/3 of="$OUT/bind" bs=256 count=1 status=none iflag=nonblock || true
else
  : > "$OUT/bind"
fi
exit 0
"""


class GlassesTests(unittest.TestCase):
    def test_usb_and_video_are_independent(self):
        with tempfile.TemporaryDirectory() as directory:
            usb = Path(directory)
            device = usb / "3-6"
            device.mkdir()
            (device / "idVendor").write_text("35ca\n")
            (device / "product").write_text("VITURE PRO 2 XR Glasses")
            result = detect([{"name": "DP-1", "description": "Unrelated monitor", "width": 1920}], usb)
            self.assertTrue(result["usb"])
            self.assertEqual(result["displays"], [])
            (device / "idVendor").unlink()
            result = detect([{"name": "DP-2", "model": "VITURE", "width": 1920}], usb)
            self.assertFalse(result["usb"])
            self.assertEqual(result["displays"], ["DP-2"])
            self.assertEqual(detect([{"name": "DP-2", "model": "VITURE", "width": 1920, "disabled": True}], usb)["displays"], [])

    def test_controller_allowlist(self):
        with tempfile.TemporaryDirectory() as directory:
            driver = Path(directory)
            (driver / "USBC000:00").symlink_to(driver)
            (driver / "USBC001:00").mkdir()
            (driver / "USBC-unsafe").symlink_to(driver)
            self.assertEqual(controllers(driver), ["USBC000:00"])

    @patch("glasses.shutil.which", return_value="/usr/bin/pkexec")
    @patch("glasses.controllers", return_value=["USBC000:00"])
    @patch("glasses.subprocess.Popen")
    def test_async_recovery_and_retry(self, popen, candidates, which):
        process = Mock()
        process.poll.return_value = None
        popen.return_value = process
        recovery = Recovery()
        recovery.start()
        command = popen.call_args.args[0]
        self.assertEqual(command, ["pkexec", "/bin/sh", "-c", RESET_SCRIPT, "omarchy-xr-reset", "USBC000:00"])
        self.assertTrue(recovery.status()["recovering"])
        with self.assertRaises(RuntimeError):
            recovery.start()
        process.poll.return_value = 126
        self.assertIn("cancelled", recovery.status()["recoveryMessage"])
        recovery.start()
        process.poll.return_value = 0
        state = recovery.status()
        self.assertFalse(state["recovering"])
        self.assertIn("Waiting for video", state["recoveryMessage"])
        self.assertTrue(state["canReset"])

    @patch("glasses.os.kill")
    @patch("glasses.worker_pid", return_value=None)
    @patch("glasses.shutil.which", return_value="/usr/bin/pkexec")
    @patch("glasses.controllers", return_value=["USBC000:00"])
    @patch("glasses.subprocess.Popen")
    def test_recovery_timeout_signals_shell_after_work_starts(self, popen, candidates, which, worker, kill):
        process = Mock()
        process.pid = 10
        process.poll.return_value = None
        process.wait.side_effect = subprocess.TimeoutExpired(["pkexec"], 2)
        popen.return_value = process
        clock = {"now": 1000.0}
        recovery = Recovery()
        with patch("glasses.time.monotonic", side_effect=lambda: clock["now"]):
            recovery.start()
            clock["now"] = 1300
            self.assertTrue(recovery.status()["recovering"])
            kill.assert_not_called()
            worker.return_value = 4242
            recovery.status()
            clock["now"] = 1419
            self.assertTrue(recovery.status()["recovering"])
            kill.assert_not_called()
            clock["now"] = 1421
            waiting = recovery.status()
            self.assertTrue(waiting["recovering"])
            self.assertIs(recovery.process, process)
            kill.assert_called_once_with(4242, signal.SIGTERM)
            process.poll.return_value = 143
            finished = recovery.status()
        self.assertFalse(finished["recovering"])
        self.assertIsNone(recovery.process)
        self.assertIn("took too long", finished["recoveryMessage"])
        self.assertNotIn(signal.SIGKILL, [call.args[1] for call in kill.call_args_list])

    def test_term_after_unbind_binds_the_controller(self):
        # bind is a fifo, so the script stops after unbind. Open it only once TERM is
        # no longer pending; opening earlier lets the main write finish and skips the trap.
        harness = BIND_HARNESS
        with tempfile.TemporaryDirectory() as directory:
            outcome = Path(directory)
            env = os.environ.copy()
            env.update(RESET=RESET_SCRIPT, OUT=directory)
            proc = subprocess.run(
                ["unshare", "--user", "--map-root-user", "--mount", "/bin/sh", "-c", harness],
                env=env, capture_output=True, text=True, timeout=20, check=False)
            self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
            device = "USBC000:00"
            self.assertEqual((outcome / "unbind").read_text(), device)
            self.assertEqual((outcome / "code").read_text().strip(), "143")
            self.assertEqual((outcome / "stderr").read_text(), "")
            bind = (outcome / "bind").read_text()
            self.assertTrue(bind and bind.count(device) * len(device) == len(bind))

    @patch("glasses.subprocess.Popen")
    def test_ambiguous_or_missing_controller_does_not_reset(self, popen):
        for devices in ([], ["USBC000:00", "USBC001:00"]):
            with patch("glasses.controllers", return_value=devices):
                with self.assertRaises(RuntimeError):
                    Recovery().start()
        popen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
