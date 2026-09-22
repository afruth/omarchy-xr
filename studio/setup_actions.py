#!/usr/bin/env python3
"""Run user-requested XR setup actions from Monitor Studio."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def run(action, which=shutil.which, runner=subprocess.run):
    packaged_setup = which("omarchy-xr-setup")
    if action in ("controls", "notifications") and packaged_setup:
        runner([packaged_setup, "--" + action], check=True)
        return
    if action == "helper":
        runner(["sudo", "/usr/bin/python3", "-I", str(ROOT / "scripts/install-helper.py")], check=True)
        return
    if action == "controls":
        runner([sys.executable, str(ROOT / "scripts/install-controls.py")], check=True)
        runner(["hyprctl", "reload"], check=True)
        runner(["hyprctl", "configerrors"], check=True)
        return
    if action == "notifications":
        runner([sys.executable, str(ROOT / "scripts/install-notifications.py")], check=True)
        return
    raise ValueError("Unknown setup action: " + action)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("helper", "controls", "notifications"))
    args = parser.parse_args()
    try:
        run(args.action)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
