#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
aliases_file="$repo_root/tools/ks2-aliases.sh"

echo "Installing local git aliases for this repo..."
git -C "$repo_root" config --local alias.st 'status -sb'
git -C "$repo_root" config --local alias.smoke '!make smoke'
git -C "$repo_root" config --local alias.check '!make check-version'
git -C "$repo_root" config --local alias.rv '!cat VERSION'
echo "Local git aliases installed: st, smoke, check, rv"

shell_name="${SHELL##*/}"
if [ "$shell_name" = "zsh" ]; then
  rc_file="$HOME/.zshrc"
else
  rc_file="$HOME/.bashrc"
fi

source_line="source \"$aliases_file\""

if [ "${1:-}" = "--write-rc" ]; then
  if [ -f "$rc_file" ] && grep -Fq "$source_line" "$rc_file"; then
    echo "Shell aliases already sourced in $rc_file"
  else
    echo "" >> "$rc_file"
    echo "# ks2 workflow aliases" >> "$rc_file"
    echo "$source_line" >> "$rc_file"
    echo "Added shell alias source line to $rc_file"
  fi
  echo "Open a new shell (or run: source \"$rc_file\")"
else
  echo ""
  echo "To enable shell aliases now:"
  echo "  source \"$aliases_file\""
  echo ""
  echo "To persist aliases in your rc file:"
  echo "  $aliases_file is ready; run this installer with --write-rc"
fi
