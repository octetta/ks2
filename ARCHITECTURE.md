# KS2 Architecture

## Purpose

This document describes the current synthesizer/runtime architecture and key control/data flows.

Use this with:

- `STATUS.md` for current command/capability snapshot
- `CHANGELOG.md` for chronological evolution
- `DEVELOPING.md` for workflow and tooling setup

## High-Level System Map (Mermaid)

```mermaid
flowchart LR
  User[User / Script File] --> REPL[src/main.c REPL]
  REPL --> DSL[src/ksynth.c DSL Evaluator]
  DSL --> Vars[(K Variables e.g. W)]
  REPL --> HostCmds[Host Commands]

  HostCmds --> Engine[src/dsp.c Synth Engine]
  HostCmds --> AudioCtl[src/audio.c Audio Control]

  Engine --> Render[ksynth_engine_render]
  Render --> AudioCB[miniaudio callback]
  AudioCB --> Out[(Audio Device)]

  HostCmds --> Banks[(Host slot bank 0-F)]
  HostCmds --> Tables[(Engine wavetable slots 0-3)]
  HostCmds --> Samples[(Engine sample slots 0-F)]

  REPL --> History[(~/.ks2 or %USERPROFILE%\\.ks2)]
```

## Runtime Layers

1. DSL layer (`src/ksynth.c`)
- Evaluates KSynth expressions and scripts.
- Produces vectors (`K`) used as wavetable/sample source data.

2. Host/REPL layer (`src/main.c`)
- Parses `:` commands and maps them to engine APIs.
- Handles history, script execution, and interrupt behavior.

3. DSP/Engine layer (`src/dsp.c`)
- Voice allocation/rendering.
- Wavetable + sample playback.
- Envelopes, modulation, filter, channel mode, mono/poly, glide.

4. Audio I/O layer (`src/audio.c`, `src/miniaudio.c`, `vendor/miniaudio/miniaudio.h`)
- Real-time callback and device output.

## Module Relationships (Graphviz)

```dot
digraph ks2_modules {
  rankdir=LR;
  node [shape=box, style=rounded];

  main [label="src/main.c\nREPL + host commands"];
  ksynth [label="src/ksynth.c\nDSL evaluator"];
  dsp [label="src/dsp.c\nengine + voices"];
  audio [label="src/audio.c\naudio glue"];
  miniaudio [label="src/miniaudio.c\nMINIAUDIO_IMPLEMENTATION"];
  ksh [label="include/ksynth.h\npublic API"];
  uedit [label="include/uedit.h\nWindows line editor"];
  bestline [label="vendor/bestline\nLinux/macOS line editor"];
  patches [label="ks/*.ks\npatch scripts"];

  main -> ksh;
  main -> ksynth;
  main -> dsp;
  main -> audio;
  main -> uedit;
  main -> bestline;
  main -> patches;

  audio -> ksh;
  audio -> miniaudio;
  dsp -> ksh;
  ksynth -> ksh;
}
```

## Command Path: Wavetable Voice (Mermaid Sequence)

```mermaid
sequenceDiagram
  participant U as User
  participant R as REPL (main.c)
  participant D as DSL (ksynth.c)
  participant E as Engine (dsp.c)
  participant A as Audio (audio.c/miniaudio)

  U->>R: :load ks/dw8k-01.ks
  R->>D: k_eval_script(file_text)
  D-->>R: W variable vector

  U->>R: :wt 0 W
  R->>E: ksynth_engine_set_table(0, W)

  U->>R: :usewt 0 1
  R->>E: ksynth_engine_use_tables(0, 1)

  U->>R: :noteon 0 60 110
  R->>E: ksynth_engine_note_on_ch(0, 60, vel)

  loop audio callback
    A->>E: ksynth_engine_render()
    E-->>A: float samples
  end
```

## Voice Architecture (Mermaid)

```mermaid
flowchart TD
  NoteOn[note_on_ch] --> Alloc[Voice Allocator]
  Alloc --> Voice[Voice State]

  Voice --> OscA[Osc A\nwave/table slot]
  Voice --> OscB[Osc B\nwave/table slot]
  OscA --> Mix[Osc Mix + Detune]
  OscB --> Mix

  Voice --> EnvAmp[Amp Env]
  Voice --> EnvPD[PD Env]
  Voice --> EnvPitch[Pitch Env]
  Voice --> LFO[LFO]

  Mix --> PD[Phase Distortion]
  PD --> Filt[Filter]
  Filt --> Amp[Apply Amp Env * Velocity]
  EnvAmp --> Amp
  EnvPD --> PD
  EnvPitch --> OscA
  EnvPitch --> OscB
  LFO --> OscA
  LFO --> OscB
```

## Channel/Performance Model

- 16 logical channels (`0..F` from REPL commands).
- Per-channel mode:
  - `poly`: independent note-triggered voice allocation.
  - `mono`: one active voice + note stack.
- Mono glide (`:glide <ch> <ms>`) sets portamento/glissando time.
- Current event API (pre-MIDI integration):
  - `ksynth_engine_note_on_ch(channel, note, velocity)`
  - `ksynth_engine_note_off_ch(channel, note)`

## Transport + Interrupt Behavior

- Sequencer transport starts stopped by default.
- `:start` / `:stop` control transport state.
- `Ctrl-C` policy:
  - first press: warn + silence active voices
  - second press: exit cleanly

## Data Banking Model

- Host banks (REPL-visible): `0..F`, tracked as wavetable/sample kind for user-facing management.
- Engine wavetable slots: `0..3` (for oscillator table routing).
- Engine sample slots: `0..F` (sample playback voices).

## Current Design Tradeoffs

- Rich engine modulation can color wavetable audition (`:playwt`) by design.
- `:playwtraw` exists for cleaner oscillator/table audition.
- Wavetable and sample workflows coexist in one runtime path, but deeper unification remains ongoing.

## Known Evolution Targets

- MIDI adapter layer on top of existing channel event API.
- Larger/dynamic voice counts beyond current fixed caps.
- Multi-synth personality modes (DW/CZ/ESQ/PPG style control surfaces).
- More explicit routing/state inspection commands (e.g., channel state introspection).
