# Migration Guide

## Purpose

This project is intended to move cleanly between machines. The safest way to do that is to keep the full workspace in GitHub and treat this repo as the source of truth.

## Document Roles

- `STATUS.md`: current snapshot of what works now.
- `CHANGELOG.md`: chronological history of changes.
- `MIGRATION.md` (this file): machine and workflow migration.
- `DEVELOPING.md`: daily CLI workflow, aliases, and commit best practices.
- `ARCHITECTURE.md`: synthesizer/runtime architecture with Mermaid + Graphviz diagrams.
- `AGENTS.md`: tool-neutral instructions for coding agents.

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
- `vendor/`
- `VERSION`
- `Makefile`
- `STATUS.md`
- `CHANGELOG.md`
- `MIGRATION.md`

Do not rely on local build outputs traveling with you. They are intentionally ignored by `.gitignore`.

## Reconnecting With Codex

On a new machine:

1. Open the repo in VS Code.
2. Start a new Codex chat.
3. Point Codex first at:
   - `AGENTS.md`
   - `STATUS.md`
   - `CHANGELOG.md`
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
- REPL history is stored locally in `.ks2` under your home directory (`~/.ks2` on Linux/macOS, `%USERPROFILE%\\.ks2` on Windows).
- If you start a fresh Codex session later, use `STATUS.md` first for current state, then `CHANGELOG.md` for recent history.
- For day-to-day dev workflow conventions (aliases, commit style), see `DEVELOPING.md`.
- Versioning is SemVer-based with `VERSION` as source of truth (`make version`, `make check-version`).
- GitHub Actions:
  - CI workflow: `.github/workflows/ci.yml` (build + version checks on push/PR).
  - Release workflow: `.github/workflows/release.yml` (push `v*` tag, requires tag version to match `VERSION`).
- Repo-stored CLI aliases:
  - Temporary shell aliases: `source tools/ks2-aliases.sh`
  - Local git aliases + setup help: `make aliases`
  - Persist shell aliases into your rc file: `make aliases-persist`
