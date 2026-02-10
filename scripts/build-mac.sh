#!/bin/bash
set -euo pipefail

# Usage: scripts/build-mac.sh <arch> <pkg>
#   arch: arm64 | x86_64
#   pkg:  darwin-arm64 | darwin-x64

ARCH="${1:?Usage: build-mac.sh <arch> <pkg>}"
PKG="${2:?Usage: build-mac.sh <arch> <pkg>}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT_DIR/binaries/$PKG/bin/tray"

mkdir -p "$(dirname "$OUT")"
clang -arch "$ARCH" -framework Cocoa -fobjc-arc -Os -o "$OUT" "$ROOT_DIR/src-mac/main.m"
strip "$OUT"
echo "Built $(wc -c < "$OUT" | tr -d ' ') bytes → $OUT"
