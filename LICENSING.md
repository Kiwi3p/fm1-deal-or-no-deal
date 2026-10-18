# Licensing and credits

This firmware is GPL-3.0-only (full text in `LICENSE`). If you ever share the firmware, share its
complete source under the same licence. Keep the inherited copyright notices, the GPL text, the SDK
Apache-2.0 notice and the font licences.

The game, its 16-bit art, soundtrack and voice lines (`firmware/src/app/{dond,ui}.c`,
`firmware/src/dsp/gen16.*`, `tools/gen16_*.py`) are original to this project and generated at
build time. The voice is rendered with espeak-ng (GPL-3.0) during the build only; espeak-ng is not
part of the firmware.

The platform is inherited from FoMni (commit 405ef41aef75f7e70afd6decc01505df07c708e9), as
credited below.

## Upstream (FoMni) licensing

FoMni is free software under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). It is built on Felucca's platform layer (through X0X)
and keeps Felucca's licence. If you distribute FoMni, or firmware derived from it, you must give
your recipients its complete corresponding source under the same licence.

### Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| Platform: `firmware/hal/`, `firmware/loader/`, `firmware/src/{libc,lcd,gfx,usb,storage,ota,midi_uart}.c`, `tools/` (build, package, install, rescue), `web/fm1*.js` | [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for [X0X](https://github.com/charlesvestal/fm1-x0x) and FoMni | GPL-3.0-only |
| `firmware/src/app/{panel,plat_fm1,main_fm1}.c`, `host/omni_host.c`, `web/emu/` | adapted from X0X (itself from Felucca's `panel.c`, `main.c`, `audio.c`) | GPL-3.0-only |
| The idea of a browser emulator for FM-1 firmware (`web/emu/`) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| `firmware/src/dsp/omni.c` (audio engine shell), `tests/` | FoMni | GPL-3.0-only |

### Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the UI face | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

### Trademarks

"Deal or No Deal" is a trademark of its owners. "Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.
"M-VAVE" and "FM-1" are trademarks of their respective owners. This is independent firmware; it is
not affiliated with, endorsed by or supported by any of them.

### Radio

This firmware never enables the Bluetooth / Wi-Fi radio of the hardware.
