# Privacy

Omarchy XR processes desktop pixels and headset pose locally to render your
workspace. It stores layouts, controls, environment preferences and diagnostic
logs in your user state/data directories. Desktop notification text is mirrored
into a per-user runtime file for the stereo view. It is not sent to the project
maintainer. Logs may include device identifiers, monitor names and local paths;
review them before sharing them in an issue.

The application does not contain its own analytics or update telemetry and does
not initialize the optional VITURE statistics reporter. The included proprietary
SDK is a separate component: VITURE's SDK agreement says it may collect usage
data unless you opt out. We cannot promise that a closed runtime makes no
network connections. Read [VITURE's privacy policy](https://www.viture.com/privacy-policy)
and [SDK terms](https://www.viture.com/viture-sdk-license-agreement) before use.
Contact VITURE about the SDK's data collection and opt-out mechanism.

Installing or updating through AUR, GitHub, or the Omarchy marketplace contacts
those services. Their own privacy policies apply. Image import uses local files;
Omarchy XR does not upload those files.

Removing the package keeps your personal layouts and images. The distribution
instructions explain how to remove local data separately.
