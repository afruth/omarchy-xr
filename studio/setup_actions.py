#!/usr/bin/env python3
"""Run user-requested XR setup actions from Monitor Studio."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def run(action, which=shutil.which, runner=subprocess.run, root=ROOT):
    packaged_setup = which("omarchy-xr-setup")
    # Prefer this Studio's own installers: an older package would install controls it no longer accepts.
    bundled = root / ("scripts/install-" + action + ".py")
    if action in ("controls", "notifications") and packaged_setup and not bundled.is_file():
        runner([packaged_setup, "--" + action], check=True)
        return
    if action == "helper":
        runner(["sudo", "/usr/bin/python3", "-I", str(root / "scripts/install-helper.py")], check=True)
        return
    if action == "controls":
        runner([sys.executable, str(bundled)], check=True)
        runner(["hyprctl", "reload"], check=True)
        runner(["hyprctl", "configerrors"], check=True)
        return
    if action == "notifications":
        runner([sys.executable, str(bundled)], check=True)
        return
    raise ValueError("Unknown setup action: " + action)


def run_all(which=shutil.which, runner=subprocess.run, root=ROOT):
    """Stereo helper, controls and notifications; each step runs even if an earlier one failed."""
    steps = [("helper", "Stereo helper"), ("controls", "Shortcuts & gestures"), ("notifications", "XR notifications")]
    if which("omarchy-xr-setup"):
        # The package owns the stereo helper; pacman keeps it in step.
        steps = steps[1:]
    failed = []
    for action, title in steps:
        print(f"\n== {title} ==", flush=True)
        try:
            run(action, which, runner, root)
        except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
            print(f"{title} failed: {error}", flush=True)
            failed.append(title)
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("all", "helper", "controls", "notifications"))
    args = parser.parse_args()
    failed = []
    try:
        if args.action == "all":
            failed = run_all()
        else:
            run(args.action)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        failed = [str(error)]
    print("\nSetup finished with problems: " + "; ".join(failed) if failed else "\nSetup finished.", flush=True)
    # Studio opens this in a new terminal; keep it open so the result can be read.
    if sys.stdin.isatty():
        input("Press Enter to close this window. ")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
