#!/usr/bin/env python3
"""Install this project's native Omarchy panel and compiled renderer locally."""
from pathlib import Path
import os
import shutil
import json

root = Path(__file__).resolve().parent.parent
if not (root / "build/omarchy-xr").is_file():
    raise SystemExit("Build first: make")
target = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home()/".config"))) / "omarchy/plugins/afruth.omarchy-xr"
if target.exists() and (not (target/"manifest.json").exists() or json.loads((target/"manifest.json").read_text()).get("id") != "afruth.omarchy-xr"):
    raise SystemExit("Refusing to overwrite an unrelated plugin directory")
for file in ("manifest.json", "LICENSE", "studio/MonitorStudio.qml", "studio/backend.py"):
    destination = target / file
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root/file, destination)
(target/"bin").mkdir(exist_ok=True)
# Atomic replacement allows installing while an older viewer is running.
shutil.copy2(root/"build/omarchy-xr", target/"bin/omarchy-xr.new")
(target/"bin/omarchy-xr.new").replace(target/"bin/omarchy-xr")
print(target)

applications = Path(os.environ.get("XDG_DATA_HOME", str(Path.home()/".local/share"))) / "applications"
applications.mkdir(parents=True, exist_ok=True)
(applications/"omarchy-xr-studio.desktop").write_text("""[Desktop Entry]
Type=Application
Name=XR Monitor Studio
Comment=Arrange virtual monitors for Omarchy XR
Exec=omarchy-shell shell summon afruth.omarchy-xr {}
Icon=video-display
Terminal=false
Categories=Settings;HardwareSettings;
""")
