#!/usr/bin/env bash
# Source this file from your shell rc for ks2 workflow helpers.
# Example:
#   source /path/to/ks2/tools/ks2-aliases.sh

alias kcheck='make check-version'
alias ksmoke='make smoke'
alias kst='git status -sb'
alias kdiff='git diff'
alias kadd='git add -p'
alias kpush='git push'

# Commit helper: kcm "your message"
kcm() {
  if [ $# -lt 1 ]; then
    echo "usage: kcm \"commit message\""
    return 1
  fi
  git commit -m "$*"
}

# Release helper: tag from VERSION and push the tag.
krel() {
  local v
  v="$(cat VERSION 2>/dev/null)" || return 1
  if [ -z "$v" ]; then
    echo "VERSION is empty"
    return 1
  fi
  git tag "v${v}" && git push origin "v${v}"
}
