# KSynth Status

## Document Roles

- `STATUS.md`: current-state snapshot of behavior, commands, and near-term direction.
- `CHANGELOG.md`: chronological history of changes over time.
- `MIGRATION.md`: machine-transfer and environment recovery workflow.

## Project Shape

KSynth is currently a native C build with three active layers:

- A restored KSynth DSL in [src/ksynth.c](/home/stewartj/book/ks2/src/ksynth.c), based on the older root-level `ksynth.c` semantics rather than the earlier temporary command-language work.
- A real-time DSP engine in [src/dsp.c](/home/stewartj/book/ks2/src/dsp.c) that still contains the richer synth experiments: polyphonic voices, 8-stage envelopes, phase distortion, filter, LFO, sequencer, wavetable support.
- A native REPL shell in [src/main.c](/home/stewartj/book/ks2/src/main.c) using `bestline` on Linux/macOS and [include/uedit.h](/home/stewartj/book/ks2/include/uedit.h) on Windows, with miniaudio playback via [src/audio.c](/home/stewartj/book/ks2/src/audio.c).

The current direction is:

- Keep the old KSynth patch/buffer workflow as the semantic center.
- Preserve the richer DSP engine for later integration.
- Be explicit about the difference between wavetable patches and one-shot sample patches.

## What Works Now

### DSL / patch execution

- `src/ksynth.c` evaluates KSynth-style scripts with one-letter globals, right-associative parsing, functions, scan `\`, vector operations, and `W` output semantics.
- `k_get` / `k_call` signatures are consistent with [include/ksynth.h](/home/stewartj/book/ks2/include/ksynth.h).
- `k_eval_script()` handles newline-separated `.ks` files properly.

### REPL / shell

Build and run:

```bash
make
./ksynth
```

Current REPL commands:

- `:help`
- `:version`
- `:quit`
- `:load <file[.ks]>` (`.ks` guessed when extension is omitted)
- `:script <file>` (`.ks2.txt`, `.txt`, `.ks2`, `.ks` guessed when extension is omitted)
- `:stop`
- `:start`
- `:playwt <var>`
- `:playsample <var>`
- `:play <var>` as legacy alias for sample playback
- `:wt <0-3> <var>`
- `:usewt <0-3> <0-3>`
- `:sample <hex> <var>`
- `:slot <hex> <var>` as legacy alias for sample banking
- `:trigsample <hex> <note> <db>` (fractional MIDI notes supported, e.g. `60.5`; gain in dB)
- `:lfo <rate_hz> <depth>`
- `:pd <amount>`
- `:filter <cutoff_hz> <res>`
- `:cutoff <hz>`
- `:res <value>`
- `:keytrack <0..1.5>`
- `:filtermode <lp|bp|hp>`
- `:fdrive <0.1..12>`
- `:gain <db>` (master synth gain in dB, `-96.0..+24.0`; values above `0 dB` intentionally allow overdrive/clipping)
- `:detune <cents_a> <cents_b>`
- `:envamp <a_ms> <d_ms> <s> <r_ms>`
- `:envpd <a_ms> <d_ms> <s> <r_ms>`
- `:envpitch <a_ms> <d_ms> <s> <r_ms>`
- `:envdepth <pitch|pd|filter> <amount>`
- `:modstate`
- `:chmode <hex> <mono|poly>`
- `:glide <hex> <ms>`
- `:pan <hex> <-1..1>`
- `:panspread <hex> <0..1>`
- `:panlfo <hex> <0..1>`
- `:chsenddelay <hex> <db>`
- `:delay <ms> <feedback> <wet>`
- `:noteon <hex> <note> <vel127>`
- `:noteoff <hex> <note>`
- `:trigwt <hex> <note> <vel127>` (channel note-on convenience alias for wavetable voice triggering)
- `:chstate <hex>` (prints channel mode/glide/held-note/active-voice snapshot)
- `:sleep <seconds|ms>`
- `:ls [path|pattern]` (directory listing plus simple wildcard patterns like `*.ks` or `ks/dw8k-0?.ks`)
- `:cd [path]` (with no argument, prints current directory)
- `:kspath [path]` (show/set KS asset base path for `:load`/`:script` fallback)
- `:playwtraw <var>`
- `:slots`

REPL history persistence:

- Linux/macOS: uses `bestline` history loaded/saved from `~/.ks2`.
- Windows: appends entered lines to `%USERPROFILE%\\.ks2` (or `%HOMEDRIVE%%HOMEPATH%\\.ks2`) and restores last command for `uedit` Up-arrow recall.

REPL completion:

- Linux/macOS (`bestline`) now provides basic tab-completion for `:` host commands.
- Non-`:` lines (KSynth DSL/K input) intentionally do not receive command completion.

REPL interrupt behavior:

- `Ctrl-C` once: does not exit; prints a warning and silences active synth voices.
- `Ctrl-C` twice in a row: exits the REPL and runs normal cleanup.

Startup behavior:

- Sequencer transport now starts stopped by default (no need to run `:stop` after launch unless you started transport manually).

REPL script runner:

- `:script <file>` executes REPL lines from a text file.
- Blank lines and lines beginning with `#` are ignored.
- Works with timing commands such as `:sleep 250ms`.
- `:load`/`:script` now use `:kspath` fallback when direct relative paths do not resolve.
- `KS2_KSPATH` environment variable can predefine `:kspath` at startup.

Versioning:

- Project version is sourced from `VERSION` (Semantic Versioning format).
- Binary reports version in the REPL banner and via `:version`.
- Build helpers:
  - `make version`
  - `make check-version`
- CI/Release automation:
  - `.github/workflows/ci.yml` for push/PR validation.
  - `.github/workflows/release.yml` for `v*` tag releases (tag must match `VERSION`).

### Patch compatibility

These old-style patches load and evaluate correctly:

- [ks/dm-bell.ks](/home/stewartj/book/ks2/ks/dm-bell.ks)
- [ks/mod-crush-rez.ks](/home/stewartj/book/ks2/ks/mod-crush-rez.ks)

These `dw8k-*` patches are treated as wavetable-style outputs:

- [ks/dw8k-01.ks](/home/stewartj/book/ks2/ks/dw8k-01.ks)
- [ks/dw8k-03.ks](/home/stewartj/book/ks2/ks/dw8k-03.ks)
- [ks/dw8k-07.ks](/home/stewartj/book/ks2/ks/dw8k-07.ks)
- and the rest of the `dw8k-##.ks` set currently in `ks/`

### Runtime distinction now in place

There are now two explicit host-side output paths:

- Wavetable path:
  - audition with `:playwt W`
  - bank with `:wt 1 W`
- Sample path:
  - audition with `:playsample W`
  - bank with `:sample 2 W`

This matters because:

- `dw8k-*` patches generate loopable wavetable content
- `dm-bell` / `mod-crush-rez` generate one-shot sample content

### Wavetable audition control

- `:playwt` uses the full synth voice path (LFO, detune, envelopes, filter, PD can all color the result).
- `:playwtraw` minimizes those modulation/depth settings for cleaner wavetable audition.
- The new modulation/envelope commands can be used to dial behavior between raw and fully-shaped playback.

### Performance Layer (Pre-MIDI)

- The engine now has channel-oriented event APIs (`note_on_ch`, `note_off_ch`) with per-channel mode and glide.
- Per-channel mode supports:
  - `poly`: independent voice allocation per note
  - `mono`: one active voice per channel with note-stack priority and glide between held notes
- `:glide` controls mono glissando time in milliseconds per channel.
- `:chstate` exposes channel runtime state (mode, glide, held notes, stack top, active voices) for debugging/control-surface work.
- Audio path now renders true stereo in the engine/audio callback (no longer mono-duped at output).
- Per-channel spatial/FX controls now include pan center, per-voice pan spread, pan LFO depth, and delay send.
- Global stereo delay is available with per-channel send routing.

## Recent Structural Decisions

### 1. Do not treat the old root `ksynth.c` as production code verbatim

The root-level [ksynth.c](/home/stewartj/book/ks2/ksynth.c) is being used as a semantic reference, not as unquestioned code. The goal is to preserve the old language model while improving maintainability and integration.

### 2. Do not force synth control into K syntax

The project is moving toward:

- KSynth DSL for array/math/buffer generation
- host commands for runtime actions like load, play, bank, transport

That split seems to fit the real workflow better than trying to express all engine control in K-like notation.

### 3. Preserve the richer DSP work

The engine code in [src/dsp.c](/home/stewartj/book/ks2/src/dsp.c) is intentionally still present even though the DSL was refocused around old KSynth semantics. It is expected to be integrated later rather than discarded.

## Current File Roles

- [src/ksynth.c](/home/stewartj/book/ks2/src/ksynth.c): restored DSL evaluator and script execution
- [include/ksynth.h](/home/stewartj/book/ks2/include/ksynth.h): public API for DSL + engine
- [src/dsp.c](/home/stewartj/book/ks2/src/dsp.c): richer synth engine and current playback backend
- [src/main.c](/home/stewartj/book/ks2/src/main.c): REPL, host commands, banking
- [src/audio.c](/home/stewartj/book/ks2/src/audio.c): audio callback wrapper
- [src/miniaudio.c](/home/stewartj/book/ks2/src/miniaudio.c): separate miniaudio implementation unit for faster rebuilds
- [vendor/miniaudio/miniaudio.h](/home/stewartj/book/ks2/vendor/miniaudio/miniaudio.h): vendored miniaudio header
- [vendor/bestline/](/home/stewartj/book/ks2/vendor/bestline): line editor used on Linux/macOS
- [include/uedit.h](/home/stewartj/book/ks2/include/uedit.h): line editor fallback used on Windows
- [ks/]( /home/stewartj/book/ks2/ks ): patch examples and compatibility targets
- [readme.md](/home/stewartj/book/ks2/readme.md), [guide.md](/home/stewartj/book/ks2/guide.md), [reference.md](/home/stewartj/book/ks2/reference.md): intent/reference docs

## Known Limitations

- `:playsample` is a lightweight sample audition path, not a finished sampler architecture.
- `:trigsample` accepts fractional MIDI notes (`0.0-127.0`) and dB gain (`-96.0` to `0.0`).
- Windows `uedit` remains single-entry interactive recall even though `.ks2` keeps a log of entered lines.
- The richer DSP engine and the restored old KSynth patch model are both present, but not yet deeply unified.
- Browser/WASM host integration is not the current native shell focus yet.
- The REPL help/examples are serviceable, but still not a polished end-user shell.

## Good Next Steps

Recommended order:

1. Decide how DSL-generated assets should feed the richer engine:
   - generated wavetable -> oscillator source
   - generated one-shot -> sample slot
   - generated vectors -> sequence or modulation sources
2. Gradually integrate the advanced engine features behind the restored DSL model instead of alongside it.
3. Only consider bytecode/VM work after the language/runtime semantics are stable.

## Machine Migration Notes

Moving between machines should be simple if the whole project folder moves together.

Recommended workflow:

1. Put this project under GitHub.
2. Commit the whole workspace, including:
   - `src/`
   - `include/`
   - `ks/`
   - `vendor/`
   - `VERSION`
   - `Makefile`
   - `STATUS.md`
3. On the new machine:
   - clone the repo
   - run `make`
   - run `./ksynth`
4. Start a new Codex chat and point it at this file first.

## Useful Example Sessions

Wavetable patch:

```text
:load ks/dw8k-01.ks
:stop
:playwt W
:wt 1 W
:slots
```

One-shot patch:

```text
:load ks/dm-bell.ks
:stop
:playsample W
:sample 2 W
:slots
:trigsample 2 60 -6
:trigsample 2 60.5 -9
```
