#!/usr/bin/env bash
# Install pre-push hook that runs tools/scrub-check (blocks push on org IP leak).
# Writes .git/hooks/pre-push. If core.hooksPath is set, updates that pre-push too.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || {
  echo "install-git-hooks: not inside a git work tree" >&2
  exit 1
}
cd "$ROOT"

HOOK_BODY='#!/bin/sh
# Athanor pre-push: scrub org-local values from tracked files, then make test.
cd "$(git rev-parse --show-toplevel)" || exit 1

echo "pre-push: scrub-check (org-local IPs/domains must not be in tracked files)"
if [ -f tools/scrub-check.sh ] && command -v python3 >/dev/null 2>&1; then
  sh tools/scrub-check.sh || exit 1
elif command -v powershell >/dev/null 2>&1; then
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/scrub-check.ps1 || exit 1
elif command -v pwsh >/dev/null 2>&1; then
  pwsh -NoProfile -File tools/scrub-check.ps1 || exit 1
else
  echo "pre-push: scrub-check unavailable (need python3 or powershell)" >&2
  exit 1
fi

if ! command -v make >/dev/null 2>&1; then
  echo "pre-push: make is not on PATH. Add the GCC bin dir, then retry."
  echo "  (operator-local MinGW/gcc bin directory)"
  exit 1
fi

echo "pre-push: make test (must match GitHub Actions)"
make test
exit $?
'

write_hook() {
  local path=$1
  mkdir -p "$(dirname "$path")"
  printf '%s\n' "$HOOK_BODY" >"$path"
  chmod +x "$path"
  echo "  wrote ${path#$ROOT/}"
}

write_hook "$ROOT/.git/hooks/pre-push"

HOOKS_PATH="$(git config --get core.hooksPath 2>/dev/null || true)"
if [ -n "$HOOKS_PATH" ]; then
  case "$HOOKS_PATH" in
    /*) ACTIVE="$HOOKS_PATH/pre-push" ;;
    *) ACTIVE="$ROOT/$HOOKS_PATH/pre-push" ;;
  esac
  write_hook "$ACTIVE"
  echo "  (core.hooksPath=$HOOKS_PATH - active hook updated)"
else
  echo "  tip: this repo normally uses: git config core.hooksPath .githooks"
fi

echo "OK - pre-push will block if lab/org.local.json values appear in tracked files."
echo "Manual check: sh tools/scrub-check.sh"
