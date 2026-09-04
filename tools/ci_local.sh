#!/bin/sh
# Local replay of GitHub Actions (DEC-0006). Same commands as .github/workflows/ci.yml
set -e
cd "$(dirname "$0")/.."
echo "=== make info ==="
make info
echo "=== make test ==="
make test
echo "=== make lib ==="
make lib
echo "=== git vs origin ==="
git fetch origin >/dev/null 2>&1 || true
git status -sb
echo "LOCAL CI OK"
