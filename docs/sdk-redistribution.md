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

## Maintainer decision and component information

The ZIP (including its nested documentation and demo archives) contains no
licence inventory or corresponding dependency source. VITURE's linked
[open-source notice](https://www.viture.com/viture-sdk-license-agreement-sider) lists LGPL libusb
and MPL Eigen for the overall SDK, without mapping them to individual binaries.
`readelf -d libglasses.so` shows only system C/C++/udev dependencies. This does
not prove that no copyleft code is statically embedded.

On 22 September 2026, the maintainer explicitly authorized proceeding with
bundling this runtime under the published application-distribution grant.
The release includes the original runtime bytes, the vendor and third-party
notices, required end-user terms, and a privacy notice. End users do not need
to obtain the developer archive separately.

A per-binary component inventory and corresponding source/relinking materials
have not been supplied. The maintainer's decision does not establish which
LGPL/MPL components are embedded or change their licence obligations. The
request below is retained for follow-up; no request has been sent to VITURE.

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
