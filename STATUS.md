# KSynth Status

## Project Shape

KSynth is currently a native C build with three active layers:

- A restored KSynth DSL in [src/ksynth.c](/home/stewartj/book/ks2/src/ksynth.c), based on the older root-level `ksynth.c` semantics rather than the earlier temporary command-language work.
- A real-time DSP engine in [src/dsp.c](/home/stewartj/book/ks2/src/dsp.c) that still contains the richer synth experiments: polyphonic voices, 8-stage envelopes, phase distortion, filter, LFO, sequencer, wavetable support.
- A native REPL shell in [src/main.c](/home/stewartj/book/ks2/src/main.c) using [include/uedit.h](/home/stewartj/book/ks2/include/uedit.h), with miniaudio playback via [src/audio.c](/home/stewartj/book/ks2/src/audio.c).

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
- `:quit`
- `:load <file.ks>`
- `:stop`
- `:start`
- `:playwt <var>`
- `:playsample <var>`
- `:play <var>` as legacy alias for sample playback
- `:wt <0-3> <var>`
- `:sample <hex> <var>`
- `:slot <hex> <var>` as legacy alias for sample banking
- `:slots`

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
- [include/uedit.h](/home/stewartj/book/ks2/include/uedit.h): line editing
- [ks/]( /home/stewartj/book/ks2/ks ): patch examples and compatibility targets
- [readme.md](/home/stewartj/book/ks2/readme.md), [guide.md](/home/stewartj/book/ks2/guide.md), [reference.md](/home/stewartj/book/ks2/reference.md): intent/reference docs

## Known Limitations

- `:playsample` is a lightweight sample audition path, not a finished sampler architecture.
- There is not yet a proper command to trigger a banked sample slot at arbitrary note/pitch from the shell.
- The richer DSP engine and the restored old KSynth patch model are both present, but not yet deeply unified.
- Browser/WASM host integration is not the current native shell focus yet.
- The REPL help/examples are serviceable, but still not a polished end-user shell.

## Good Next Steps

Recommended order:

1. Add shell commands for triggering banked sample slots at pitch, for example `:trigsample <slot> <note> <vel>`.
2. Add shell commands for using banked wavetable slots more directly, for example `:usewt <a> <b>`.
3. Decide how DSL-generated assets should feed the richer engine:
   - generated wavetable -> oscillator source
   - generated one-shot -> sample slot
   - generated vectors -> sequence or modulation sources
4. Gradually integrate the advanced engine features behind the restored DSL model instead of alongside it.
5. Only consider bytecode/VM work after the language/runtime semantics are stable.

## Machine Migration Notes

Moving between machines should be simple if the whole project folder moves together.

Recommended workflow:

1. Put this project under GitHub.
2. Commit the whole workspace, including:
   - `src/`
   - `include/`
   - `ks/`
   - `miniaudio/`
   - `include/uedit.h`
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
```
