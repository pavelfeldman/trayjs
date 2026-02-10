#!/usr/bin/env bash
set -euo pipefail

# Publishes all @trayjs packages to npm.
# Native platform packages are published first, then the main package.
#
# Usage:
#   ./scripts/publish.sh [--dry-run]

DRY_RUN=""
if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN="--dry-run"
  echo "Dry run mode enabled."
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
NATIVE_PACKAGES=(linux-x64 linux-arm64 darwin-x64 darwin-arm64 win32-x64 win32-arm64)

# Verify all binaries are present before publishing anything.
missing=()
for pkg in "${NATIVE_PACKAGES[@]}"; do
  if [[ "$pkg" == win32-* ]]; then
    bin="$ROOT_DIR/binaries/$pkg/bin/tray.exe"
  else
    bin="$ROOT_DIR/binaries/$pkg/bin/tray"
  fi
  if [[ ! -f "$bin" ]]; then
    missing+=("binaries/$pkg/bin/tray${pkg##win32-*}")
  fi
done

if [[ ${#missing[@]} -gt 0 ]]; then
  echo "Error: missing binaries:"
  printf '  %s\n' "${missing[@]}"
  echo "Run ./scripts/download-artifacts.sh <run-id> first."
  exit 1
fi

# Publish native packages (cd into each to avoid workspace resolution).
for pkg in "${NATIVE_PACKAGES[@]}"; do
  echo "Publishing @trayjs/$pkg..."
  (cd "$ROOT_DIR/binaries/$pkg" && npm publish $DRY_RUN --access public)
done

# Publish main package.
echo "Publishing @trayjs/trayjs..."
(cd "$ROOT_DIR" && npm publish $DRY_RUN --access public)

echo "Done."
