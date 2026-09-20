#!/usr/bin/env python3
"""Install a user-obtained SDK locally; never copy it into the public repository."""
import argparse
import os
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("library_directory", type=Path, help="Directory containing Linux libglasses.so and its dependencies")
args = parser.parse_args()
source = args.library_directory.resolve()
if not (source / "libglasses.so").is_file():
    parser.error("Choose the Linux SDK directory containing libglasses.so")
target = Path(os.environ.get("XDG_DATA_HOME", str(Path.home()/".local/share"))) / "omarchy-xr/sdk"
target.mkdir(parents=True, exist_ok=True)
for library in source.glob("*.so*"):
    if library.is_file():
        temp = target / (library.name + ".new")
        shutil.copy2(library, temp)
        temp.replace(target / library.name)
print(f"SDK installed in {target}. Choose Connect glasses in Monitor Studio.")
