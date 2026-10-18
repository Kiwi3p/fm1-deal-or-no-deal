# Deal or No Deal — 16-bit edition for the M-VAVE FM-1

Custom firmware that turns the M-VAVE FM-1 into a Sega Genesis–style Deal or No Deal game:
26 cases on the keys, a pixel-art host, an FM/PSG chiptune soundtrack and a robot voice that
reads out the banker's offers. All art, music and voice lines are generated from scratch
at build time (`tools/gen16_*.py`); no ROM or show assets are used.

The firmware replaces only the FM-1's app region, using the Felucca USB-MIDI updater. The
bootloader and device data are never written, so the stock firmware can always be restored.

## Install on the FM-1

You need a Mac/PC with Python 3, a USB cable, and a built package
(`build/dealornodeal-preview.fwsc`, see [Building](#building)).

1. **Close anything that uses MIDI** (DAWs, browser tabs with Web MIDI).
2. **Set up the installer** (once):

   ```sh
   python3 -m venv .venv
   . .venv/bin/activate
   python -m pip install -r requirements-install.txt
   ```

3. **Plug in the FM-1, switch it on, and check it's found:**

   ```sh
   python tools/fm1_install.py --info
   ```

   This should print a line like `FM-1_... [running] port: ...`. If it finds nothing, replug
   the cable and close other MIDI apps.

4. **Install:**

   ```sh
   python tools/fm1_install.py build/dealornodeal-preview.fwsc
   ```

   Check the listed port, then type `y`. Keep the cable connected until the installer says
   `done: the FM-1 runs FM-1_710`. The FM-1 restarts into the title screen.

If an install is interrupted, run the same command again. **Never pass `--force`.** If the
installer refuses a device or package, stop and find out why.

### Recovery

| Situation | What to do |
| --- | --- |
| Game is running, you want to reinstall or go back to other firmware | Hold **OCT− + OCT+ for 5 seconds** → update mode, then run `fm1_install.py` with any package |
| Firmware crashes on boot twice | FM-1 shows **SAFE MODE** (USB still works): reinstall |
| Four failed boots | Chip enters **UBOOT**: use `tools/fm1_rescue.py` with M-VAVE's stock **FM-1 V15** `FM-1.fwsc` (see the script header; dry run first) |

⚠️ Do **not** hold OCT− + OCT+ while *switching on*: that opens panel calibration, which
overwrites the user-preset area.

It's worth keeping a copy of M-VAVE's stock V15 `FM-1.fwsc` (from m-vave.com) for the UBOOT rescue.

## How to play

| Control | Action |
| --- | --- |
| Any key / PLAY on the title | Start |
| Leftmost 26 keys | Cases 1–26 (lit = still closed) |
| PLAY | DEAL · (final) KEEP your case · skip a case reveal |
| SEL | NO DEAL · (final) SWAP |
| HOME | Show/hide the board of remaining amounts |
| GLO | New game (SELECT knob picks Yes/No, then SEL) |
| MASTER knob | Volume |

Pick your case, then open 6, 5, 4, 3, 2 and then 1 case per round. After each round the banker
calls with an offer. The board of remaining amounts shows itself after your pick and before each
offer; press any key to dismiss it. At the end, see whether your deal beat your case.

## Building

Needs Python 3, Git, a C compiler, `espeak-ng` (for the voice) and, on macOS or ARM, **Docker
Desktop running** (the JieLi compiler is Linux x86-64 only).

```sh
brew install espeak-ng            # macOS; apt install espeak-ng on Linux
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-build.txt -r requirements-install.txt

# one-time: JieLi toolchain + AC79 SDK into tooling/ (gitignored)
sh tools/get_toolchain.sh "$PWD/tooling/jieli"
git clone --branch AC79NN_SDK_V1.2.1_2023-12-13 --depth 1 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK tooling/ac79-sdk

export JIELI_TOOLCHAIN="$PWD/tooling/jieli/toolchain"
export AC79_SDK="$PWD/tooling/ac79-sdk"
export PYTHON="$PWD/.venv/bin/python"
export X0X_FONTSET=terminus

sh tests/run_tests.sh             # host simulator + game logic tests
sh build.sh                       # -> build/dealornodeal-preview.fwsc (identity FM-1_710)
```

The build checks the image fits the FM-1 (app slot, RAM and pool) and fails if it doesn't.
More detail, plus the native simulator and browser preview: [BUILDING.md](BUILDING.md).

## Project layout

| Path | Role |
| --- | --- |
| `firmware/src/app/dond.c` | Game rules / state machine |
| `firmware/src/app/ui.c` | Scenes, animation, input, music and voice cues |
| `firmware/src/app/main_fm1.c` | Device main loop |
| `firmware/src/dsp/gen16.c` | FM + PSG synth, sequencer, ADPCM voice player |
| `tools/gen16_art.py` | Generates the pixel art (`--preview DIR` writes PNGs) |
| `tools/gen16_music.py` | Generates the soundtrack |
| `tools/gen16_voice.py` | Renders the voice lines with espeak-ng |
| `tools/fm1_install.py`, `tools/fm1_rescue.py` | USB-MIDI installer and UBOOT rescue |
| `host/`, `web/emu/` | Native simulator and browser preview |
| `tests/` | Logic, scenario and platform tests |
| `docs/DOND-GENESIS-PLAN.md` | Design notes for the 16-bit edition |

## Licence

GPL-3.0-only, inherited from the Felucca platform this is built on. See [LICENSING.md](LICENSING.md)
for credits. "Deal or No Deal" is a trademark of its owners; this is an unofficial, personal fan
project with no affiliation. M-VAVE and FM-1 are trademarks of their respective owners.
