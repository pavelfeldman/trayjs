#!/usr/bin/env bash
set -euo pipefail

# Downloads native binaries from a GitHub Actions run and places them
# into the git-ignored packages/*/bin/ directories.
#
# Usage:
#   ./scripts/download-artifacts.sh <run-id>
#   ./scripts/download-artifacts.sh 21808342397

REPO="pavelfeldman/trayjs"
PACKAGES=(linux-x64 linux-arm64 darwin-x64 darwin-arm64 win32-x64 win32-arm64)

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <github-actions-run-id>"
  exit 1
fi

RUN_ID="$1"

command -v gh >/dev/null 2>&1 || { echo "Error: gh CLI is required. Install from https://cli.github.com"; exit 1; }

echo "Downloading artifacts from run $RUN_ID..."

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

gh run download "$RUN_ID" --repo "$REPO" --dir "$TMPDIR"

for pkg in "${PACKAGES[@]}"; do
  echo "  $pkg"
  mkdir -p "packages/$pkg/bin"
  if [[ -d "$TMPDIR/$pkg/bin" ]]; then
    # Unix artifacts: full package with bin/ subdirectory
    cp -f "$TMPDIR/$pkg/bin/"* "packages/$pkg/bin/"
  else
    # Windows artifacts: bare .exe in artifact root
    cp -f "$TMPDIR/$pkg/"*.exe "packages/$pkg/bin/"
  fi
done

# Make Unix binaries executable.
for pkg in "${PACKAGES[@]}"; do
  bin="packages/$pkg/bin/tray"
  [[ -f "$bin" ]] && chmod +x "$bin"
done

echo "Done. Binaries placed in packages/*/bin/"
