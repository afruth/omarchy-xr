# Distribution validation — 22 September 2026

Candidate: 0.3.0, Arch Linux x86_64. Source branch:
packaging merged in PR #11, including notification PR #10. The maintainer
has authorized bundling the unchanged SDK runtime (see sdk-redistribution.md).

Executed checks:

- Optimized renderer build with compiler warnings treated as errors.
- `make check`: C++ unit programs, Lua behavior suite, Python suite and CLI errors.
- All 115 Python tests pass, including the isolated package lifecycle, loading the staged vendor library and
  checking that it reports version 2.4.0 without opening the glasses.
- `make check-notifications`: content, stereo rendering, expiry, dismissal,
  placement and motion regressions pass.
- `make check-ui`: 54 QML checks pass.
- Updated full Monitor Studio loads in an isolated, offscreen Quickshell preview;
  the real XR backend is disconnected from that preview.
- Ruff and mypy pass. Existing Lua complexity warnings, a missing QML lint stub,
  and two pre-existing oversized test functions mean full legacy lint is not
  clean. No newly added function exceeds the function-length check.
- The release builder produces a complete application archive and concrete AUR
  PKGBUILD/.SRCINFO with matching SHA-256, not SKIP or placeholder checksums.
- `makepkg --cleanbuild --force` checks installed dependencies, verifies the
  archive checksum and builds the actual pacman package.
- The staged package plugin and a clean source snapshot pass the installed
  `omarchy plugin validate` consumer. The old tracked developer-path symlink was
  removed; no vendor binary is included in the source checkout.
- `desktop-file-validate` passes; dynamic renderer dependencies resolve locally.
- Isolated package tests run actual installed scripts and the Omarchy CLI using
  Bubblewrap. First-use consent, both optional integrations, repeat setup,
  backups, removal and retained user data pass. Updating the system renderer
  is picked up by the plugin launcher without rerunning setup. Only shell and
  compositor IPC are simulated; no real desktop or hardware is changed.
- AUR RPC reports no existing omarchy-xr, omarchy-xr-bin or omarchy-xr-git package
  at the time of the check. Recheck before publication.

The package has not been installed over the user's running XR session.
The source and v0.3.0 release are now public. All three release downloads were
fetched without GitHub credentials and checked against the published SHA256SUMS.
A fresh anonymous source clone passes the installed Omarchy plugin validator.
The AUR recipe is ready; submission needs an account-linked SSH key. Marketplace
submission does not imply maintainer approval. The component information and
maintainer's bundling decision are recorded in sdk-redistribution.md.
