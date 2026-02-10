#!/bin/bash
set -euo pipefail

# Usage: scripts/build-linux.sh <pkg>
#   pkg: linux-x64 | linux-arm64

PKG="${1:?Usage: build-linux.sh <pkg>}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT_DIR/binaries/$PKG/bin/tray"
SRC="$ROOT_DIR/src-linux"

mkdir -p "$(dirname "$OUT")"

CFLAGS=$(pkg-config --cflags gtk+-3.0 ayatana-appindicator3-0.1)
LIBS=$(pkg-config --libs gtk+-3.0 ayatana-appindicator3-0.1)

gcc -O2 -Wall -o "$OUT" "$SRC/main.c" "$SRC/cJSON.c" $CFLAGS $LIBS -lpthread
strip "$OUT"
echo "Built $(wc -c < "$OUT" | tr -d ' ') bytes → $OUT"
