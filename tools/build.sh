#!/bin/sh
# Portable Athanor builder (DEC-0004 / DEC-0006). Delegates to Makefile.
set -e
cd "$(dirname "$0")/.."
CC=${CC:-cc}
export CC
make test
