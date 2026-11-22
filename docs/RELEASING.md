# CWTrainerZero release preparation

Developed for Tartanleaf.com by Greg Weir VA1GW

This procedure prepares an ARM64 Debian package for the Linux-based **M5Stack
Cardputer Zero**. It does not publish, submit, sign, install, or change the
device. The application is not ESP32 Cardputer firmware.

## Prepare and build

Start in a clean checkout of the exact release source. Run
`scripts/check-source-export.sh` and record the revision. Follow the simulator
build and CTest checks in the README. For the ARM64 build, use a disposable
Debian 13 builder (or equivalent native toolchain environment) with CMake 3.25+,
Ninja, Git, GNU ARM64 GCC/G++, and dpkg packaging tools. The CMake recipe pins
LVGL and miniaudio; the first configure needs Git access unless the exact
pre-fetched trees are supplied with `CW_LVGL_SOURCE` and `CW_MINIAUDIO_SOURCE`.

Obtain the official [v0.0.4 BSP](https://github.com/CardputerZero/launcher/releases/download/v0.0.4/sdk_bsp.tar.gz).
Confirm the downloaded file is 435,042,027 bytes. Verify its SHA-256 and extract
it into the toolchain's default project-local path so CMake's nested compiler
checks find it too. Use a fresh checkout without a pre-existing sysroot:

```sh
printf '%s  %s\n' \
  e51b6eb803ed08f450e459efbfe62dd0341440846f3be9d01da861fe6cfdebb0 \
  /path/to/sdk_bsp.tar.gz | sha256sum -c - || exit 1
mkdir -p .cache/sdk_bsp-src
tar -xzf /path/to/sdk_bsp.tar.gz -C .cache/sdk_bsp-src

cmake -S . -B build/cardputerzero -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/cardputerzero-aarch64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCM0_ALLOW_FETCH_DEPS=OFF \
  -DCW_CARDPUTER_ZERO=ON -DCW_ENABLE_DEB=ON -DBUILD_TESTING=OFF \
  -DCW_APP_ICON="$PWD/packaging/icon.png" \
  '-DCW_DEB_MAINTAINER=Greg Weir VA1GW <gweir@tartanleaf.com>'
cmake --build build/cardputerzero --parallel
cpack --config build/cardputerzero/CPackConfig.cmake -B build/package -G DEB
```

The maintainer contact above is for the official Tartanleaf.com release only.
Forks supply their own approved contact with the same flag; CMake leaves the
default unset. The committed PNG matches the reviewed launcher icon embedded in
the package; the editable SVG is beside it. Do not expect undocumented toolchain
versions or native build paths to produce identical archive bytes.

## Verify and prepare Store material

Set `deb` to the one newly built package, then review and record:

```sh
dpkg-deb -f "$deb" Package Version Architecture Maintainer Depends
dpkg-deb -c "$deb"
sha256sum "$deb"
```

Check package/executable/launcher names, ARM64 ELF and dependencies against the
target BSP, the package-specific icon and launcher paths, and the included MIT
and third-party notices. Do not add package binaries, SDK files, build output,
logs, local settings, credentials, or device footage to Git.

[app-builder.json](../app-builder.json) holds Store metadata and uses the
[official AppBuilder schema](https://github.com/CardputerZero/AppBuilder/blob/main/docs/APP_BUILDER_JSON.md).
The five checked-in 320×170 PNGs are native-size **desktop simulator captures**.
They are not physical-device evidence:

1. [Beginner lesson](../screenshots/listen.png): Start with K and M.
2. [Answer feedback](../screenshots/checked.png): Check what you heard.
3. [Full alphabet](../screenshots/all-letters.png): Jump straight to all 26 letters.
4. [Numbers](../screenshots/numbers.png): Practise digits in a group of four.
5. [Punctuation](../screenshots/punctuation.png): Choose a symbol with the built-in picker.

To refresh them intentionally, run the desktop simulator workflow test; it
creates `listen.bmp`, `checked.bmp`, `all-letters.bmp`, `numbers.bmp`, and `punctuation.bmp` in the desktop build
directory's `smoke/` subdirectory. Convert to PNG without resizing or overlays,
inspect all images, and include only the intended Store images in the change.

Recheck Store title and share-code availability at submission; the draft code
is not a reservation. Keep the intended source URL in step with the actual
public repository. [Device acceptance](DEVICE-TEST.md) and the official
[publishing lifecycle](https://github.com/CardputerZero/packages/blob/main/docs/PUBLISHING.md)
require a short physical-device demo for a first-time Store submission. A
simulator capture is not that demo. Keep it separate from Git and submit it only
through the approved Store workflow.
