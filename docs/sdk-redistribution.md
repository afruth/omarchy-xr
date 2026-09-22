# SDK redistribution review — 22 September 2026

The locally supplied archive is VITURE XR Glasses SDK 2.4.0 for Linux x86_64.
The release builder pins its SHA-256 and copies only its unchanged
`x86_64/libglasses.so` into the complete application bundle. The SDK is not
committed to git, made available as a standalone download, or uploaded to AUR.

## Permission and conditions

The [official SDK agreement](https://www.viture.com/viture-sdk-license-agreement),
effective September 2025, section 1.3 grants object-code distribution as a
component of a developed application. Section 2 limits standalone publication,
requires end-user restrictions, indemnity and notices, and restricts trademark
claims. Section 8 calls for a privacy notice. This supports application bundling,
not republication of the developer archive. The application terms and privacy
notice implement those distribution conditions. This is a technical reading of
the published terms, not a vendor-specific written approval.

Runtime SHA-256: `a68cbef5e39fa97086fe6e8d63a2a3649165c78c298f85ba896ff12ef75404ad`.

## Remaining release evidence

The ZIP (including its nested documentation and demo archives) contains no
licence inventory or corresponding dependency source. VITURE's linked
[open-source notice](https://www.viture.com/viture-sdk-license-agreement-sider) lists LGPL libusb
and MPL Eigen for the overall SDK, without mapping them to individual binaries.
`readelf -d libglasses.so` shows only system C/C++/udev dependencies. This does
not prove that no copyleft code is statically embedded.

Before publicly publishing the bundled runtime, obtain VITURE's component
inventory for this precise binary and any corresponding source/relinking
materials required for embedded LGPL/MPL components, or confirmation those
components occur only in the omitted Carina/OpenCV/demo runtime. The public
notice and generic upstream licence texts alone cannot prove this obligation
is satisfied. No request has been sent to VITURE on the user's behalf.

The build and local install tests can proceed while this is resolved. The
archive is a release candidate, not evidence that this outstanding distribution
question is closed. Do not publish it or mark the distribution goal complete
until the evidence is recorded here.

## Vendor request ready to send

We are preparing Omarchy XR, an independent Linux spatial desktop for Gen1/Gen2
glasses. We want to distribute unchanged `libglasses.so` from your Linux x86_64
SDK 2.4.0 solely inside our application, with your notices, required end-user
terms, and a privacy notice. We will not publish the developer ZIP, headers,
demos, Carina/VIO or OpenCV libraries.

Please provide the third-party component/version inventory for this binary and
confirm whether it embeds libusb or other LGPL code, or MPL-covered code. If so,
please provide the corresponding source, modifications and relinking material
needed for application redistribution. Please also confirm the supported SDK
telemetry opt-out mechanism when the statistics reporter is not initialized.
