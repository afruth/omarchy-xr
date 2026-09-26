#!/usr/bin/env python3
"""Build a complete x86_64 application release and a matching AUR submission."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SDK_SHA256 = "9ceab9ecbcae0185b903cd14d17848f32b0750768a21f956b768ed18796759fc"


def copy(source, target, mode=0o644):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    target.chmod(mode)


def plugin_files():
    spec = importlib.util.spec_from_file_location("studio_installer", ROOT / "scripts/install-studio.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.PLUGIN_FILES


def install_tree(stage, renderer, sdk_archive):
    """Stage only owned package paths. Never touch a user's session/config."""
    if hashlib.sha256(sdk_archive.read_bytes()).hexdigest() != SDK_SHA256:
        raise ValueError("SDK archive differs from the audited 2.4.0 Linux x86_64 archive")
    share = stage / "usr/share/omarchy-xr"
    plugin = share / "plugin"
    for name in plugin_files():
        copy(ROOT / name, plugin / name)
    copy(renderer, stage / "usr/bin/omarchy-xr", 0o755)
    copy(ROOT / "bin/omarchy-xr", plugin / "bin/omarchy-xr", 0o755)
    copy(ROOT / "scripts/package-setup.py", stage / "usr/bin/omarchy-xr-setup", 0o755)
    for filename in ("install-studio.py", "install-controls.py", "install-notifications.py"):
        copy(ROOT / "scripts" / filename, share / "scripts" / filename)
    copy(ROOT / "config/xr-controls.lua", share / "config/xr-controls.lua")
    for source in (ROOT / "notifications").iterdir():
        if source.is_file():
            copy(source, share / "notifications" / source.name)
    copy(ROOT / "studio/dedicated_helper.py", stage / "usr/lib/omarchy-xr/omarchy-xr-display", 0o755)
    copy(ROOT / "packaging/io.github.afruth.omarchy-xr.display.policy",
         stage / "usr/share/polkit-1/actions/io.github.afruth.omarchy-xr.display.policy")
    copy(ROOT / "packaging/70-omarchy-xr.rules", stage / "usr/lib/udev/rules.d/70-omarchy-xr.rules")
    copy(ROOT / "packaging/omarchy-xr.desktop", stage / "usr/share/applications/omarchy-xr.desktop")
    for name in ("LICENSE", "PRIVACY.md", "THIRD_PARTY_NOTICES.md"):
        copy(ROOT / name, share / name)
        copy(ROOT / name, stage / "usr/share/licenses/omarchy-xr-bin" / name)
    for source in (ROOT / "packaging/licenses").iterdir():
        copy(source, stage / "usr/share/licenses/omarchy-xr-bin" / source.name)
    for name in ("distribution.md", "sdk-redistribution.md", "marketplace-submission.md", "window-canvas.md"):
        copy(ROOT / "docs" / name, stage / "usr/share/doc/omarchy-xr" / name)
    # Extract one known member, never SDK headers, source, demo, or standalone archive.
    runtime = stage / "usr/lib/omarchy-xr/sdk/libglasses.so"
    runtime.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(sdk_archive) as sdk:
        runtime.write_bytes(sdk.read("x86_64/libglasses.so"))
    runtime.chmod(0o755)
    return runtime


def write_aur(destination, version, archive):
    destination.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    template = (ROOT / "packaging/aur/PKGBUILD.in").read_text()
    template = template.replace("@VERSION@", version).replace("@SHA256@", digest)
    (destination / "PKGBUILD").write_text(template)
    with (destination / ".SRCINFO").open("w") as info:
        subprocess.run(["makepkg", "--printsrcinfo"], cwd=destination, stdout=info, check=True)
    copy(ROOT / "LICENSE", destination / "LICENSE")


def archive_tree(stage, archive, epoch):
    with tarfile.open(archive, "w:xz") as tar:
        for path in sorted(stage.rglob("*")):
            info = tar.gettarinfo(str(path), str(path.relative_to(stage)))
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            info.mtime = epoch
            if path.is_file():
                with path.open("rb") as stream:
                    tar.addfile(info, stream)
            else:
                tar.addfile(info)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-archive", type=Path, required=True)
    parser.add_argument("--renderer", type=Path, default=ROOT / "build/omarchy-xr")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    version = json.loads((ROOT / "manifest.json").read_text())["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        parser.error("Release version must be numeric major.minor.patch")
    header = args.renderer.read_bytes()[:20]
    if header[:6] != b"\x7fELF\x02\x01" or int.from_bytes(header[18:20], "little") != 62:
        parser.error("Renderer must be a Linux x86_64 ELF binary")
    actual = subprocess.check_output([str(args.renderer.resolve()), "--version"], text=True).strip()
    if actual != "omarchy-xr " + version:
        parser.error("Rebuild the renderer: its version does not match manifest.json")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", subprocess.check_output(
        ["git", "log", "-1", "--format=%ct"], cwd=ROOT, text=True).strip()))
    archive = output / f"omarchy-xr-{version}-linux-x86_64.tar.xz"
    with tempfile.TemporaryDirectory(prefix="omarchy-xr-package-") as temp:
        install_tree(Path(temp), args.renderer, args.sdk_archive)
        archive_tree(Path(temp), archive, epoch)
    write_aur(output / "aur", version, archive)
    (output / "SHA256SUMS").write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name + "\n")
    print(archive)
    print("AUR submission:", output / "aur")


if __name__ == "__main__":
    main()
