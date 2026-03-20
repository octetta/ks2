# Migration Guide

## Purpose

This project is intended to move cleanly between machines. The safest way to do that is to keep the full workspace in GitHub and treat this repo as the source of truth.

## Move To Another Machine

### 1. Push the current machine to GitHub

If this is the first push:

```bash
git add .
git commit -m "Initial KSynth native DSL and runtime workspace"
git branch -M main
git remote add origin <your-github-repo-url>
git push -u origin main
```

For later updates:

```bash
git add .
git commit -m "Describe the change"
git push
```

### 2. Clone on the new machine

```bash
git clone <your-github-repo-url>
cd ks2
```

If you use a different folder name locally, that is fine.

### 3. Build and run

```bash
make
./ksynth
```

## What Must Travel With The Repo

Keep these in Git:

- `src/`
- `include/`
- `ks/`
- `miniaudio/`
- `Makefile`
- `STATUS.md`
- `MIGRATION.md`

Do not rely on local build outputs traveling with you. They are intentionally ignored by `.gitignore`.

## Reconnecting With Codex

On a new machine:

1. Open the repo in VS Code.
2. Start a new Codex chat.
3. Point Codex first at:
   - `STATUS.md`
   - `MIGRATION.md`
4. Then describe the current task.

This should recover project context much faster than relying on memory.

## First Checks On A New Machine

Recommended sanity checks:

```bash
make
./ksynth
```

Then in the REPL:

```text
:help
:load ks/dw8k-01.ks
:playwt W
:load ks/dm-bell.ks
:playsample W
```

## Notes

- `:playwt` is for wavetable-style patch outputs such as `dw8k-*`.
- `:playsample` is for one-shot sample outputs such as `dm-bell` and `mod-crush-rez`.
- If you start a fresh Codex session later, `STATUS.md` is the best single-file project handoff.
