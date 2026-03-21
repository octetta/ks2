# Changelog

This project follows a simple, practical changelog style.

## How To Use This File

- `CHANGELOG.md`: chronological record of what changed and when.
- `STATUS.md`: current snapshot of capabilities and current direction.
- `MIGRATION.md`: how to move the project between machines and reconnect quickly.

When updating features, keep `CHANGELOG.md` and `STATUS.md` in sync:

1. Add the change entry here (chronological).
2. Update `STATUS.md` so current behavior is accurate.

## Versioning Policy

- Versioning uses Semantic Versioning (SemVer): `MAJOR.MINOR.PATCH` with optional pre-release/build metadata.
- Source of truth is the `VERSION` file in repo root.
- Releases should be tagged as `v<version>` (example: `v1.2.3`).
- Recommended release flow (CI-ready):
  1. Update `VERSION`.
  2. Update `CHANGELOG.md`.
  3. Run `make check-version && make smoke`.
  4. Commit and tag `v<version>`.

## [Unreleased]

### Added

- GitHub Actions workflows:
  - CI: `.github/workflows/ci.yml`
  - Release: `.github/workflows/release.yml`
- Cross-platform line editor routing:
  - Linux/macOS use `vendor/bestline`.
  - Windows uses `include/uedit.h`.
- Vendored `miniaudio` moved to `vendor/miniaudio`.
- Persistent REPL history in `.ks2`:
  - Linux/macOS via `bestline` history load/save.
  - Windows logs lines to `.ks2` and restores last command for Up-arrow recall.
- New REPL synth/performance commands:
  - `:trigsample` with fractional MIDI note and dB gain.
  - `:lfo`, `:pd`, `:detune`.
  - `:envamp`, `:envpd`, `:envpitch`, `:envdepth`.
  - `:modstate`.
  - `:chmode`, `:glide`, `:noteon`, `:noteoff`.
  - `:usewt`.
  - `:sleep`.
  - `:script`.
- Pre-MIDI channel performance layer in DSP:
  - Per-channel mono/poly mode.
  - Mono note-stack behavior.
  - Per-channel glide/glissando for mono mode.
- `make smoke` target for quick regression checks.
- SemVer scaffolding:
  - `VERSION` file as source of truth.
  - `make version` and `make check-version`.
  - runtime `:version` command and versioned REPL banner.
- Demo script: `ks/demo-dw-dual.ks2.txt`.

### Changed

- Startup transport defaults to stopped.
- `:playwtraw` command introduced to minimize modulation for raw wavetable audition.
- REPL input handling now ignores comment/fence lines in pasted blocks:
  - `# ...`
  - `// ...`
  - ```` ```... ``` ```` lines

### Fixed

- `:playwtraw` parsing precedence so it is not consumed by `:playwt`.
- Double `Ctrl-C` interrupt behavior:
  - first press warns/silences voices,
  - second press exits cleanly.
