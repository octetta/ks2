# AGENTS Guide

## Purpose

This file defines repo-level collaboration rules for any coding agent.
It is intentionally tool-neutral and should work for Codex and non-Codex agents.

## Source Of Truth

Use these files together:

- `STATUS.md`: current capabilities and behavior snapshot.
- `CHANGELOG.md`: chronological change history.
- `MIGRATION.md`: machine migration and environment recovery.
- `DEVELOPING.md`: daily workflow, aliases, commit style.
- `VERSION`: SemVer source of truth.

If behavior changes, update `STATUS.md` and `CHANGELOG.md` in the same change set.

## Build And Validation

From repo root:

```bash
make
make check-version
make smoke
```

Before committing, run at least:

```bash
make check-version
make smoke
```

## Versioning And Releases

- Versioning uses Semantic Versioning.
- `VERSION` is the single source of truth.
- Release tags must be `v<version>` and match `VERSION`.
- CI and release workflows live in `.github/workflows/`.

## Change Hygiene

1. Keep commits focused (one logical behavior change per commit).
2. Prefer intentional staging (`git add -p`) over broad staging.
3. Do not revert unrelated user changes.
4. Preserve existing docs and examples unless behavior truly changed.
5. When adding/changing REPL commands, update:
   - REPL help text in `src/main.c`
   - command list/notes in `STATUS.md`
   - `CHANGELOG.md` entry

## Safety Notes

- Audio/DSP changes in `src/dsp.c` can affect runtime behavior globally; keep edits minimal and test with `make smoke`.
- REPL command parser order matters in `src/main.c` (more specific patterns should be checked before more general ones).
- Keep platform behavior explicit:
  - Linux/macOS line editing via `vendor/bestline`
  - Windows fallback via `include/uedit.h`

## Cross-Agent Compatibility

- Avoid assumptions about one specific agent runtime.
- Prefer standard shell commands and plain markdown docs.
- If adding tool-specific notes, keep them in clearly labeled optional sections and do not make core workflow depend on them.
