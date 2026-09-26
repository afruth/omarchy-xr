# Marketplace submission draft

Repository URL: https://github.com/afruth/omarchy-xr
Category: Hardware
Tags: Hyprland, Quickshell, Workspaces

## Maintainer notes

XR Monitor Studio creates a spatial desktop using independently captured virtual
monitors. It currently works with VITURE XR glasses only, including supported
Gen1/Gen2 models, and includes head tracking, stereo output, workspace layouts
and optional spatial notifications.

Requires Arch Linux x86_64, Omarchy 4's Quickshell plugin system and companion
package `omarchy-xr-bin`. The package supplies the renderer, licensed proprietary
SDK runtime, udev rules and fixed polkit display helper. The marketplace checkout
contains QML/Python UI source and uses that system runtime. No binaries or SDK
headers are committed to the plugin repository.

Reserved-rights application licence: `LicenseRef-Omarchy-XR` (LICENSE). Vendor
and open-source dependency terms are separately preserved. The included runtime
is not a standalone SDK distribution. Read PRIVACY.md and THIRD_PARTY_NOTICES.md.

Install the companion package from the public v0.4.0 release before enabling
the plugin, or use the plugin’s **Install XR runtime** button. It downloads
the complete release package, checks a pinned SHA-256 and invokes pacman with
visible confirmation prompts. AUR is not required. Run
`omarchy-xr-setup --controls --notifications` to accept terms and explicitly opt
into the two optional integrations. Setup keeps existing marketplace checkouts;
package installation never rewrites user configuration. Removal is documented
in docs/distribution.md. The display helper is the only privileged component;
its narrowly scoped polkit action permits the active local session.

## Submission status

The repository and v0.4.0 assets are public, and a fresh source clone passes
`omarchy plugin validate`. Direct package installation is documented and usable
without AUR. The submission is open at
https://github.com/omacom/omarchy-plugin-marketplace/issues/8144. Marketplace
approval is complete and does not imply a security review.

Submit at https://github.com/omacom/omarchy-plugin-marketplace/issues/new?template=submit-plugin.yml
