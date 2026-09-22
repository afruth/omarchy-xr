# Distribution

Target: Arch Linux x86_64, Omarchy 4 with the Quickshell plugin system and
Hyprland Lua controls. Runtime package: `omarchy-xr-bin`; marketplace plugin:
`afruth.omarchy-xr`. This release uses reserved-rights application terms, not an
open-source licence. Earlier MIT-licensed versions keep their existing terms.

**Release status:** [v0.3.0 is published](https://github.com/afruth/omarchy-xr/releases/tag/v0.3.0)
with the bundled SDK, Arch package, AUR recipe and SHA-256 checksums. The source
repository is public. Installation uses GitHub releases directly and does not
require AUR. The [marketplace submission](https://github.com/omacom/omarchy-plugin-marketplace/issues/8144)
has passed structure/compatibility validation and awaits maintainer review.
AUR publication is deferred; its recipe remains available in the release.

## Install the published package

Download `omarchy-xr-bin-0.3.0-1-x86_64.pkg.tar.zst` and `SHA256SUMS` from the
release page. Verify the package hash against that file, then run:

```sh
sudo pacman -U ./omarchy-xr-bin-0.3.0-1-x86_64.pkg.tar.zst
omarchy-xr-setup --controls --notifications
```

This is the supported package installation path. It does not require an AUR account.

## Install from AUR

Once the AUR entry is published:

```sh
yay -S omarchy-xr-bin
omarchy-xr-setup --controls --notifications
```

Setup displays the application/SDK terms and privacy notice before enabling
anything. It installs the plugin for your desktop user, enables the bar button,
and opens Studio. `--controls` adds the Hyprland Lua integration after checking
for binding conflicts and backing up bindings. `--notifications` replaces the
native notification service with its XR extension, preserving native behavior.
Omit either option to leave that integration alone. Plug the glasses back in
after first installation so the new udev rules apply.

The runtime and privileged display helper are package-owned. There is no SDK
form, email link, manual SDK download, root Python invocation, or build step for
end users. The only privileged runtime operation is the fixed display helper
through polkit; it is limited to the active local session.

## Migrate an existing source installation

Before installing the package, stop XR. If you previously ran `make install-helper`,
use `make uninstall-helper` from that old checkout first so pacman does not
encounter an unowned polkit file at its installation path. Do not use pacman's
blanket `--overwrite`. Package setup backs up existing non-git plugin files;
marketplace git checkouts remain untouched. Keep the old backup until the
packaged version is working.

## Install from the Omarchy marketplace

Add the public plugin using Omarchy's standard command:

```sh
omarchy plugin add https://github.com/afruth/omarchy-xr.git --enable
```

Open XR Monitor Studio from the bar, then choose **Utilities → Connection tools
→ Install XR runtime**. The button opens a terminal, downloads the complete
v0.3.0 package from GitHub, enforces its pinned size of 2,804,714 bytes while
streaming, verifies its SHA-256 against the digest pinned in the
plugin, and runs `sudo pacman -U --needed` with normal confirmation prompts.
It then runs setup as your desktop user to display the terms and enable controls
and notifications. Neither an AUR account nor the vendor SDK download is needed.
The package manager runs only after verification; a failed download or checksum
stops installation. Oversized downloads stop before excess bytes are written;
incomplete or unverified downloads are deleted. No downloaded script is executed.

If you installed the package directly already, run
`omarchy-xr-setup --controls --notifications` to finish setup.

The checkout supplies QML and a small renderer launcher. The package supplies
the compiled renderer, licensed SDK runtime, helper and udev rules. Setup
preserves an existing marketplace git checkout and its edits. Omarchy's plugin
installer does not install OS dependencies or run a build hook. Listing this
plugin without documenting the companion package would leave an unusable UI.
Do not install a second copy if package setup already installed this plugin.

Updates: the next viewer launch uses the updated system renderer automatically.
Run `omarchy-xr-setup` again to refresh the package-installed UI. For a marketplace
checkout use `omarchy plugin update afruth.omarchy-xr` as well. Reapply `--controls` and `--notifications` to update
those optional copies; setup never resets saved layouts or imported images.
A changed licence requires acceptance before the packaged SDK is opened again.

## Remove

Stop stereo and remove virtual monitors using Studio first. Then, as your user:

```sh
omarchy-xr-setup --remove
```

This uses Omarchy's plugin removal flow (which restores its native notification
service) and removes only the exact XR require line from bindings, keeping a
backup. User control files, layouts, logs and images are retained. Remove the
system files with `sudo pacman -Rns omarchy-xr-bin` after the user integration.
If you want to delete personal data too, inspect `~/.local/state/omarchy-xr`,
`~/.local/share/omarchy-xr`, `~/.config/hypr/xr-controls.lua` and
`~/.config/hypr/xr-touchpads.lua` before deleting them. These are never removed
by package-manager hooks.

## Build a release candidate

The maintainer supplies the already-obtained vendor archive. The script checks
its digest and will not silently accept a different SDK revision:

```sh
make -j4 build/omarchy-xr
python3 scripts/package-release.py --sdk-archive /path/to/VITURE_XR_Glasses_SDK_for_Linux_x86_64.zip
```

Outputs in ignored `dist/`: a complete versioned application tarball,
`SHA256SUMS`, and an `aur/` directory with real checksums in `PKGBUILD` and
`.SRCINFO`. Never replace the archive's contents after publishing a version.
Only the complete application tarball is a release asset; never publish the
vendor ZIP or a separate runtime download. Build with current Arch dependencies.
`SOURCE_DATE_EPOCH` can pin archive timestamps.

For an offline local packaging test, copy the generated tarball into `dist/aur/`
and run `makepkg --nodeps` there. It validates the real checksum and constructs
the pacman package. `--nodeps` is for staging verification only; normal users
must install dependencies. Inspect package contents with `bsdtar -tf` and query
metadata with `pacman -Qip` before installing anything.

To exercise the installed paths without touching your desktop, extract the
pacman package into a temporary directory, then run:

```sh
XR_PACKAGE_ROOT=/path/to/extracted-package python3 -m unittest tests.test_package_install -v
```

This requires Bubblewrap with overlay support and the installed Omarchy CLI.
It runs the real setup, control/notification installers, plugin validator and
plugin removal commands in a private filesystem, with network and hardware
access removed. Shell/compositor IPC is simulated; this verifies installation,
updates and removal, not live shell rendering or head tracking.

## Publication order

1. Keep the approved SDK runtime, reserved-rights end-user terms, privacy notice
   and third-party notices together in the complete application release.
2. Make the repository and release downloads publicly accessible for AUR and
   the marketplace. Public visibility does not grant an open-source licence.
3. Tag v0.3.0 and publish the complete archive and
   checksums as GitHub release assets. Confirm the generated source URL downloads
   the archive with the recorded hash from a clean machine.
4. Check official repositories and AUR for naming collisions, then submit the
   generated AUR files. The application source is available, so the prebuilt
   application uses the `-bin` suffix.
5. Submit the prepared [marketplace listing](marketplace-submission.md). The
   marketplace requires a public repository, licence, valid manifest, dependency
   documentation, and safe installation/removal. Listing approval is external.

References: [AUR submission](https://wiki.archlinux.org/title/AUR_submission_guidelines),
[nonfree package naming](https://wiki.archlinux.org/title/Nonfree_applications_package_guidelines),
[Omarchy publishing](https://plugins.omarchy.org/publish.html).
