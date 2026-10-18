# Deal or No Deal — 16-bit edition plan

A Sega Genesis-flavoured take on the game show for the FM-1. Every pixel, note and voice line is made here, from scratch. No ripped art, audio, show music or real-person likeness, so the normal build carries all of it and nothing needs gating.

## 1. The 16-bit look, translated to the FM-1

| Genesis trait | How we copy it | FM-1 fit |
| --- | --- | --- |
| 8×8 tiles, 4 bits per pixel | All art authored as tiles; scenes are tile maps | Tiny in flash; decoded straight into the existing canvas |
| 4 palettes × 15 colours + transparent | Same limit, enforced by the art generator | One 16-entry RGB565 table per palette |
| 9-bit colour (3 bits per channel) | Every colour snapped to RGB333 | Gives the unmistakable Genesis palette |
| 320×224 screen | 240×160 scene (30×20 tiles), 80 px info strip below | Matches the current layout and the 76.8 KB canvas already in the pool |
| Ordered-dither shading, hard outlines, chunky 8×8 font | Built into the art and an 8×8 strip font | A new font is ~1 KB |
| Dialogue box with a talking portrait | Host portrait in the strip while he speaks | Reuses the face-patch animation idea |

### Scenes (all original)

- **Title:** chunky gold "DEAL OR NO DEAL" lettering drawn here (not the show's logo), sparkle sprites, and the host waving.
- **Stage:** all 26 briefcases on numbered podiums. Opened cases show their amount, faded out. This also fixes "which cases are left?" at a glance.
- **Money board:** two columns of 13 gold bars. Bars for opened amounts go dark, with a quick flash when one is knocked out.
- **Banker booth:** a shadowy figure behind tinted glass with a desk phone that shakes when it rings.
- **Final reveal:** your case opening (3-frame lid animation), the amount in large digits, and a GOOD DEAL / BAD DEAL banner.

### The host

He's an **original character**: a bald showman in a suit with a small pointed goatee, designed from scratch rather than modelled on any real person:
- Stocky build, round head with a shine highlight, bushy dark brows and a big toothy grin
- A short, sharply pointed goatee
- A navy double-breasted suit, white shirt, gold tie and red pocket square

**Sprites:**
- Full body, 32×64, 4 poses: idle, point, arms up, hand on chin.
- Portrait, 48×48, for the strip.
- Mouth frames (closed, mid, wide, "O") and a blink, stored as small patches.

## 2. Sound: a small FM chip on the FM-1

### Music and effects engine (`dsp/gen16.c`)

- **FM voices:** 4 voices × 4-operator FM in the style of the YM2612 (algorithms 0–7, envelopes, feedback).
- **PSG voices:** 3 square + 1 noise, SN76489-style, for rings, beeps and the case "pop".
- Rendered at **22,050 Hz** and doubled to the 44,100 Hz output with linear interpolation. This halves the CPU cost, and the grit suits the style.
- Sine comes from a 1,024-entry table (2 KB), with no `sinf` in the audio callback.

### Songs

Original tracker-style patterns, at a few hundred bytes each:
- title theme
- studio bed and tension loop
- phone ring
- offer, DEAL, NO DEAL and big/small win stings

None of them copy the show's music.

### Voice (`tools/gen_dond_voice.py`, offline)

1. Speech is generated with **espeak-ng** (open source, GPL-3, via Homebrew). Pitch and speed are set for a showman delivery.
2. It's then processed for the Genesis-DAC sound: band-limited to about 3.4 kHz, resampled to **8 kHz**, mildly bit-crushed, and stored as **4-bit IMA ADPCM** (4 KB per second).
3. The player decodes on the fly in the audio callback. The decoder is about 60 lines of code with no buffers.
4. You can swap in your own recording for any line with the existing voice importer.

**Lines (all original wording):**
- welcome; "pick your case"
- "open six / five / four / three / two cases" and "open one more case"
- "ooh, that hurts!", "great pick!", "the banker is calling…"
- "deal… or no deal?", "deal!", "no deal!", "keep it or swap it?"
- "let's open your case!", "good deal!", "bad deal…"

**Stretch goal: spoken offers** ("the banker offers… forty-two thousand dollars"). About 35 number words get stitched together at playback time, adding roughly 40 KB.

Mixing: voice on its own channel, music ducked about 9 dB under speech, and the host's mouth animated while voice plays.

## 3. Will it fit? Flash (app slot 581,564 B)

| Item | Estimate |
| --- | ---: |
| Current game (measured today, asset-free) | 85,192 B |
| Tiles + maps for 5 scenes (≤ 300 unique tiles each × 32 B + maps) | ~50 KB |
| Host sprites, portrait, mouth/blink patches, cases, sparkles | ~12 KB |
| 8×8 font + palettes | ~2 KB |
| FM/PSG engine + ADPCM decoder + sine table | ~12 KB |
| Songs and instrument patches | ~8 KB |
| Voice lines (~30 s at 4 KB/s) | ~120 KB |
| Spoken offers (stretch) | ~40 KB |
| **Total** | **~330 KB** |
| **Headroom** | **~250 KB** |

`tools/build.py` already reports the image size and fails if the app overflows. I'll add a per-section breakdown (art / music / voice) so growth is visible.

## 4. Will it run? RAM and CPU

- **RAM:** the canvas stays 240×160 (76.8 KB of the 344 KB pool). Tiles decode straight from flash. The synth state is under 2 KB and ADPCM needs no buffer. No new large buffers.
- **CPU:** the audio callback's worst case is 4 FM voices × 4 operators at 22 kHz, plus 4 PSG voices plus ADPCM. That's about 350k table lookups per second.
  - The device previously ran the upstream firmware's 8-voice sampler with echo and plate reverb.
  - **Gate:** `plat_cpu_pct()` under 60% and zero `plat_xruns()` on the device with title music, voice and animation all running. If it misses, drop to 3 FM voices or 2-operator patches; both are settings, not rewrites.
- **Display:** the full scene is redrawn per frame, the same as now (~14 ms of SPI at the measured ~44 MHz). Animation runs at 15–20 fps; text-only changes redraw just the strip.

## 5. Milestones

| # | Build | Done when |
| --- | --- | --- |
| G1 | **Art pipeline:** tiles authored as readable text grids in `assets/gen16/*.txt`; generator enforces the Genesis limits and writes `build/gen/gen16_art.h`; tile/sprite renderer in `gfx.c` | Five scenes and the host render in the host simulator and web preview |
| G2 | **Stage gameplay visuals:** 26-case stage, money board, booth, reveal animation | You can see every case's state without pressing HOME |
| G3 | **FM/PSG engine and songs** | Title theme, beds and stings play in the web preview |
| G4 | **Voice:** espeak-ng pipeline, ADPCM player, ducking, mouth sync | The host speaks at every cue moment |
| G5 | **Device pass:** flash, CPU and xrun gates, then the README recovery soak | Measured numbers recorded in `docs/SAMPLE-BUDGET.md` |
| G6 | (stretch) Spoken offer amounts | The offer is read aloud |

Every milestone keeps `sh tests/dond/run_tests.sh` passing and adds host-simulator screenshots per phase. The web preview (`web/emu/build.sh`) gets rebuilt at each step so you can play it.

## 6. Housekeeping

- This replaces the ripped-art scene path in `ui.c` with the tile renderer.
- `assets/gen16/` and the generator are original, GPL-3 like the rest of the code. `docs/ARTWORK-PROVENANCE.md` gets an entry.
- espeak-ng is a build-time tool only. Its generated audio ships, but its code doesn't.

## 7. Decisions (17 Oct 2026)

1. **Host:** bald, in a suit, with a small devil's goatee. Original design, as described in section 1.
2. **Voice:** robot speech (espeak-ng with 16-bit processing).
3. **Spoken offer amounts:** yes. G6 moves into scope.
4. **The full board of remaining amounts** appears at several points, not only on HOME:
   - after you pick your case (the full ladder)
   - for a few seconds after each case reveal, with the new knockout flashing
   - as a recap before every banker offer
   - when two cases are left, before keep or swap
   - at the final reveal, showing what was left

   Any key or PLAY skips the board, and HOME still toggles it anytime.

**Storage note:** scenes are stored as palette-limited (Genesis RGB333, at most 61 colours) run-length images using the renderer already in `gfx.c`, rather than 8×8 tile maps. It gives the same look with less new firmware code, and the art generator still enforces the Genesis colour rules.
