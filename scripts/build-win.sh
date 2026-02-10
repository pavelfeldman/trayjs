#!/bin/bash
set -euo pipefail

# Usage: scripts/build-win.sh <pkg>
#   pkg: win32-x64 | win32-arm64

PKG="${1:?Usage: build-win.sh <pkg>}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(cygpath -w "$ROOT_DIR/binaries/$PKG/bin/tray.exe")"
SRC="$(cygpath -w "$ROOT_DIR/src-win")"

mkdir -p "$ROOT_DIR/binaries/$PKG/bin"

# Determine linker machine flag
case "$PKG" in
  win32-x64)   MACHINE="X64"   ;;
  win32-arm64) MACHINE="ARM64" ;;
  *) echo "Unknown package: $PKG" >&2; exit 1 ;;
esac

# Locate cl.exe — when cross-compiling (e.g. arm64 on x64 host)
# the target compiler may not be on PATH.
if command -v cl.exe &>/dev/null; then
  CL="cl.exe"
else
  # Find cl.exe via VCToolsInstallDir (set by vcvarsall)
  ARCH_LOWER=$(echo "$MACHINE" | tr '[:upper:]' '[:lower:]')
  CL="$(cygpath -u "$VCToolsInstallDir")/bin/HostX64/$ARCH_LOWER/cl.exe"
  if [ ! -f "$CL" ]; then
    echo "cl.exe not found for $MACHINE" >&2; exit 1
  fi
fi

# Compilation Flags:
# /O2: Optimize for speed (standard for release)
# /MT: Statically link C Runtime (no DLL dependencies)

# /W3: Enable standard warnings
"$CL" /O2 /MT /DUNICODE /D_UNICODE /DCJSON_HIDE_SYMBOLS \
  /W3 \
  "$SRC/main.c" "$SRC/cJSON.c" \
  /Fe:"$OUT" \
  /link /SUBSYSTEM:WINDOWS /MACHINE:"$MACHINE" /OPT:REF /OPT:ICF \
  user32.lib shell32.lib gdi32.lib kernel32.lib advapi32.lib

echo "Built $(wc -c < "$OUT" | tr -d ' ') bytes → $OUT"
