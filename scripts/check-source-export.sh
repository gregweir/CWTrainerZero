#!/usr/bin/env bash
set -euo pipefail
cd -- "$(git rev-parse --show-toplevel)"
expected="$(printf '%s\n' \
  .gitignore CMakeLists.txt LICENSE README.md THIRD-PARTY-NOTICES.md app-builder.json \
  cmake/cardputer-package.cmake cmake/cardputerzero-aarch64.cmake docs/DEVICE-TEST.md docs/RELEASING.md \
  licenses/CardputerZero-Template-MIT.txt licenses/LVGL-MIT.txt licenses/Montserrat-OFL.txt licenses/miniaudio-LICENSE.txt \
  packaging/cwtrainerzero.desktop packaging/icon.svg packaging/icon.png scripts/check-source-export.sh \
  screenshots/all-letters.png screenshots/checked.png screenshots/listen.png screenshots/numbers.png screenshots/punctuation.png \
  src/audio.cpp src/audio.hpp src/lv_conf.h src/main.cpp src/trainer.cpp src/trainer.hpp tests/test_trainer.cpp \
  | LC_ALL=C sort)"
actual="$(git ls-files --cached | LC_ALL=C sort)"
diff -u <(printf '%s\n' "$expected") <(printf '%s\n' "$actual")
# Keep the upstream Montserrat notice byte-for-byte, including its trailing space.
git diff --cached --check -- . ':(exclude)licenses/Montserrat-OFL.txt'
printf '%s\n' 'Exact 30-path source allowlist verified. Review file content and intended public destinations before publishing.'
