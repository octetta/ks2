# Developing KS2

## Purpose

This file is a practical day-to-day developer guide for local workflow.

Use this alongside:

- `STATUS.md` for current behavior and capabilities
- `CHANGELOG.md` for chronological history
- `MIGRATION.md` for machine migration/setup
- `ARCHITECTURE.md` for structural diagrams and runtime topology

## Quick Setup

From repo root:

```bash
make aliases
```

Optional (persist shell aliases in your rc file):

```bash
make aliases-persist
```

## Alias Reference

### Shell aliases/functions

Defined in `tools/ks2-aliases.sh`:

- `kcheck` -> `make check-version`
- `ksmoke` -> `make smoke`
- `kst` -> `git status -sb`
- `kdiff` -> `git diff`
- `kadd` -> `git add -p`
- `kpush` -> `git push`
- `kcm "message"` -> `git commit -m "message"`
- `krel` -> tag and push from `VERSION` (`v$(cat VERSION)`)

### Local git aliases

Installed by `make aliases`:

- `git st` -> status short branch view
- `git check` -> `make check-version`
- `git smoke` -> `make smoke`
- `git rv` -> print `VERSION`

## Daily CLI Flow

Recommended loop:

```bash
kcheck
ksmoke
kst
kadd
kcm "feat(scope): concise behavior-focused message"
kpush
```

If you prefer raw git, same flow works with:

```bash
make check-version
make smoke
git status -sb
git add -p
git commit -m "..."
git push
```

## Commit Message Best Practices

Use:

```text
type(scope): what changed
```

Common `type` values:

- `feat` new capability
- `fix` regression/bug fix
- `refactor` internal change without behavior change
- `docs` docs-only change
- `build` build tooling / CI
- `test` tests-only
- `chore` maintenance

Good examples:

- `feat(repl): add :script command with sleep-aware playback`
- `fix(repl): ignore markdown fence lines in pasted input`
- `feat(engine): add mono/poly channel mode with glide`
- `build(ci): add tag-driven release workflow`
- `docs(status): document double-ctrl-c behavior`

Guidelines:

1. Keep subject line under ~72 chars.
2. Use imperative present tense (`add`, `fix`, `update`).
3. Describe behavior impact first.
4. Keep commits focused (one logical change per commit).
5. If needed, add a short body after a blank line.

## Versioning and Releases

- `VERSION` is the single source of truth.
- Validate format with:

```bash
make check-version
```

- Show current version:

```bash
make version
```

- Release tag convention: `v<version>` (must match `VERSION`).
- Fast tag/push helper:

```bash
krel
```

GitHub Actions:

- CI: `.github/workflows/ci.yml`
- Release: `.github/workflows/release.yml`

## Regression Safety Tips

- Prefer `git add -p` over `git add .` for intentional staging.
- Run `make smoke` before every commit.
- Tag known-good milestones when exploring risky changes.
- Keep `STATUS.md` and `CHANGELOG.md` updated when behavior changes.

## VS Code Diagram Preview Setup

`ARCHITECTURE.md` uses both Mermaid and Graphviz (`dot`) diagrams.

### Mermaid in Markdown

Recent VS Code builds generally render Mermaid fenced blocks in Markdown preview.

Use:

```bash
code ARCHITECTURE.md
```

Then open preview:

- `Ctrl+Shift+V` (Windows/Linux)
- `Cmd+Shift+V` (macOS)

### Graphviz (`dot`) in Markdown

Install one of these VS Code extensions:

- `tintinweb.graphviz-interactive-preview`
- `EFanZh.graphviz-preview`

Optional Graphviz CLI install (useful for validation/export):

- Ubuntu/Debian: `sudo apt-get install graphviz`
- macOS (Homebrew): `brew install graphviz`

### Tips

- Keep Mermaid blocks fenced with ` ```mermaid `.
- Keep Graphviz blocks fenced with ` ```dot `.
- If preview doesn’t update, close/reopen the Markdown preview panel.
