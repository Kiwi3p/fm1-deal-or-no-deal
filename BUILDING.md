# Building

The quick version is in the [README](README.md#building). This page has the details.

## Requirements

- Python 3 with `requirements-build.txt` (Pillow) and `requirements-install.txt` (MIDI, for installing)
- A C compiler and Git
- `espeak-ng` for the voice lines (`brew install espeak-ng`). Without it the build still works,
  but the host is silent. Rendered lines are cached in `build/gen/voice-cache`.
- The JieLi pi32v2 toolchain and AC79 SDK (below). The compiler is Linux x86-64 only. On macOS
  or ARM Linux, **Docker** must be running: `build.sh` runs the compiler in a `linux/amd64` container.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-build.txt -r requirements-install.txt
sh tools/get_toolchain.sh "$PWD/tooling/jieli"
git clone --branch AC79NN_SDK_V1.2.1_2023-12-13 --depth 1 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK tooling/ac79-sdk
export JIELI_TOOLCHAIN="$PWD/tooling/jieli/toolchain"
export AC79_SDK="$PWD/tooling/ac79-sdk"
export PYTHON="$PWD/.venv/bin/python"
export X0X_FONTSET=terminus
export JIELI_DOCKER_IMAGE='debian:bookworm-slim@sha256:7c7b2c966bc9ee8cedfeef67e0e279108992c77681fa595db4a9d65c06ccc587'
sh tests/run_tests.sh
sh build.sh
```

`tooling/`, `.venv/` and `build/` are gitignored.

Output: `build/dealornodeal-preview.fwsc` (identity **FM-1_710**), plus `build/omni.bin` and
`build/loader/ota.bin`. Building and testing never touch the hardware.

The build checks:
- the app image fits its 581,564 B slot
- RAM and the pool fit
- flash routines are RAM-only
- no soft-double maths
- hardware registers are touched only in `firmware/hal/`

Reference versions: compiler jieli-linux-toolchains-20260730.1, SDK commit d179b4484759423312073f5fbb232501aa491047.

## Generated assets

`tools/build.py` regenerates these into `build/gen/` on every build:

| Script | Output |
| --- | --- |
| `tools/gen_font.py` | UI font |
| `tools/gen16_art.py` | Scenes, host sprite, cases, fonts (`--preview DIR` writes PNGs to look at) |
| `tools/gen16_music.py` | FM/PSG songs and stings |
| `tools/gen16_voice.py` | Voice lines and number words (espeak-ng → 8-bit → IMA ADPCM) |

## Simulator and browser preview

- Native simulator: `sh host/build_host.sh`. It runs `.omni` scripts such as `tests/dond/basic_flow.omni`.
- Browser preview:
  1. Install Emscripten 4.0.23 and source its `emsdk_env.sh`.
  2. Run `sh web/emu/build.sh`.
  3. Run `python3 -m http.server 8792 --bind 127.0.0.1 --directory build/emu`.
  4. Open <http://127.0.0.1:8792>.

The preview runs the same game code. It does not match the device's timing, so always try a build on the hardware too.

## Release builds

`sh build.sh --release 1.0` makes `build/dealornodeal-1.0.fwsc` with identity `FM-1_7010000`.
