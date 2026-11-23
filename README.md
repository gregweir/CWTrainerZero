# CWTrainerZero

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Latest GitHub release](https://img.shields.io/github/v/release/gregweir/CWTrainerZero)](https://github.com/gregweir/CWTrainerZero/releases/latest)

Developed for Tartanleaf.com by Greg Weir VA1GW

Offline Morse code practice for **M5Stack Cardputer Zero** (Linux/aarch64, Raspberry Pi CM0). This is not firmware for the ESP32 Cardputer or Cardputer ADV.

Listen to a group, type what you heard, and check. The default is two letters. Start with **K** and **M**. Add one letter at a time through 25 levels, or jump directly to all 26 letters. The app uses its own Koch-style sequence: `KMURESNAPTLWIJZFOYVGQHDBCX`. You can pick any level at any time, with no score requirement. Replay keeps the same group and any partial entry. The app shows the answer only after checking. Open Practice Settings to choose a group of 1–4 characters, or switch to the separate Numbers (`0–9`) or Punctuation (`.,?/=`) set. Letter level is preserved when you switch back. [ITU-R M.1677-1](https://www.itu.int/rec/R-REC-M.1677-1-200910-I/en) supplies the Morse mappings; its `=` notation is the on-air double hyphen. There is no account, microphone recording, RF activity or background service. No Cardputer extension cap is needed.

| Key | Action |
| --- | --- |
| Space | Play or replay |
| A–Z / 0–9 / `.,?/=` | Type what you heard for the chosen set |
| F1 (Fn+1 on Cardputer Zero) | Practice Settings: Up/Down selects a row; Left/Right changes it; Enter saves; Esc cancels |
| Backspace | Edit a letter |
| Enter | Check a completed group, then move to the next group |
| Left / Right | Lower/raise effective WPM (on Cardputer Zero, Fn+Z / Fn+C) |
| Up / Down | Add/remove one lesson letter (Fn+F / Fn+X) |
| 0 | Jump to the full alphabet in Letters; type zero in Numbers (keep Fn off) |
| Tab | Hear the newest letter in Letters; insert the selected symbol in Punctuation |
| Up / Down in Punctuation | After listening, pick a symbol as an alternative to direct punctuation keys |
| Esc | Quit |

Characters always play at **18 WPM**, default **8 effective WPM** (adjustable 5–18). Farnsworth spacing lengthens only gaps between characters and words. The 600 Hz tone is generated at 48 kHz with short edge ramps; start with low system volume when using headphones. Practice set, group size, letter level and speed are saved locally in `$XDG_CONFIG_HOME/cwtrainerzero/stats-v3` (or `~/.config/cwtrainerzero/stats-v3`). Groups counts a group correct only if every character matches, and keeps your older totals. Chars counts matches by position and starts at 0/0 in v0.3, since earlier releases did not keep that information. For long-running totals, each display switches to percentages after 9,999 attempts.

When first opened without a v0.3 save, the app can read a CW1 or CW2 `stats` file in the same directory. It carries over speed, group totals and any saved letter level (CW1 starts at Level 1). The earlier file is not modified: a downgrade uses that earlier snapshot and does not show later practice. Keep both when backing up. The simulator's saved data is separate from that on the device.

## Install on Cardputer Zero

**From the Cardputer Zero Store:** Open **Store** on your device, press **5** for **Share Code**, type **`CWTZ`**, and press **Enter**. On the **CWTrainerZero** details page, choose **Install**, then open it from APPLaunch.

**Manual install:** Download the ARM64 `.deb` from [GitHub Releases](https://github.com/gregweir/CWTrainerZero/releases/latest). Check it against the supplied `SHA256SUMS`, install the package, then open **CWTrainerZero** in APPLaunch. No new system image or reflashing is needed.

See the [Cardputer Zero device guide](https://docs.m5stack.com/en/CardputerZero) or the [build instructions](docs/RELEASING.md) if you want to build the package yourself.

## Build and run the Linux simulator

Dependencies: C++17, CMake 3.25 or newer (CMake 4.2.3 tested), SDL2 development files and a CMake build generator. The LVGL and miniaudio source commits are pinned by CMake. The first configure fetches them unless given their matching pre-fetched source trees. Running the app needs no downloads.

```sh
cmake -S . -B build/desktop -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop --parallel
ctest --test-dir build/desktop --output-on-failure
./build/desktop/cwtrainerzero
```

If needed, set `-DSDL2_DIR=/path/to/SDL2/cmake`, `-DCW_LVGL_SOURCE=/path/to/lvgl` and `-DCW_MINIAUDIO_SOURCE=/path/to/miniaudio`. Use a Linux graphical session for the visible simulator. CTest can run without a screen or an audio device: the simulator smoke test uses SDL's dummy video driver and an explicitly silent miniaudio test backend. A WAV for independent playback can be made with:

```sh
./build/desktop/cwtrainerzero --wav build/desktop/letters-demo.wav KM
```

The SDL simulator uses the same LVGL UI at exactly 320 × 170, scaled up for review. The core and its tests also build without SDL/LVGL with `-DCW_BUILD_UI=OFF`.

Project source is MIT-licensed; preserve the notices when sharing or adapting it. See [LICENSE](LICENSE) and [third-party notices](THIRD-PARTY-NOTICES.md).
