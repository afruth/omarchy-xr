# Marketplace submission draft

Repository URL: https://github.com/afruth/omarchy-xr
Category: Hardware
Tags: Hyprland, Quickshell, Workspaces

## Maintainer notes

XR Monitor Studio creates a spatial desktop using independently captured virtual
monitors. Supports Gen1/Gen2 glasses and includes head tracking, stereo output,
workspace layouts and optional spatial notifications.

Requires Arch Linux x86_64, Omarchy 4's Quickshell plugin system and companion
package `omarchy-xr-bin`. The package supplies the renderer, licensed proprietary
SDK runtime, udev rules and fixed polkit display helper. The marketplace checkout
contains QML/Python UI source and uses that system runtime. No binaries or SDK
headers are committed to the plugin repository.

Reserved-rights application licence: `LicenseRef-Omarchy-XR` (LICENSE). Vendor
and open-source dependency terms are separately preserved. The included runtime
is not a standalone SDK distribution. Read PRIVACY.md and THIRD_PARTY_NOTICES.md.

Install the companion package before enabling the plugin. Run
`omarchy-xr-setup --controls --notifications` to accept terms and explicitly opt
into the two optional integrations. Setup keeps existing marketplace checkouts;
package installation never rewrites user configuration. Removal is documented
in docs/distribution.md. The display helper is the only privileged component;
its narrowly scoped polkit action permits the active local session.

## Before submitting

Do not send this draft until the release assets and AUR entry are available,
the repository is public, and `omarchy plugin validate` passes on a clean checkout of the publication
commit. Complete the official submission checklist using that actual state.

Submit at https://github.com/omacom/omarchy-plugin-marketplace/issues/new?template=submit-plugin.yml
