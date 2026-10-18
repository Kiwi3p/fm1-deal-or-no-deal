#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/dond build/gen
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
python3 tools/gen16_art.py build/gen/gen16_art.h >/dev/null
python3 tools/gen16_music.py build/gen/gen16_music.h >/dev/null
python3 tools/gen16_voice.py build/gen/gen16_voice.h >/dev/null
cc -O2 -std=c99 -Wall -Wextra -I firmware/src/app -o build/dond/dond_logic_test tests/dond/dond_logic_test.c
build/dond/dond_logic_test
sh host/build_host.sh
mkdir -p build/dond/preview
build/host/omni_host tests/dond/basic_flow.omni build/dond/preview
build/host/omni_host tests/dond/no_deal.omni build/dond/preview
cc -O2 tests/storage_test.c -o build/dond/storage_test
build/dond/storage_test
echo "dond tests: ok"
