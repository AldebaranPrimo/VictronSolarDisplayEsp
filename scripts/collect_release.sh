#!/usr/bin/env bash
set -euo pipefail

# Collect build artifacts into a top-level release/ directory.
# Files copied:
# - bootloader.bin
# - flasher_args.json
# - partition-table.bin
# - spiffs.bin
# - VictronSolarDisplayEsp.bin

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
RELEASE_DIR="$ROOT_DIR/release"

declare -A artifacts
artifacts=(
  [bootloader]="$BUILD_DIR/bootloader/bootloader.bin"
  [flasher_args]="$BUILD_DIR/flasher_args.json"
  [partition_table]="$BUILD_DIR/partition_table/partition-table.bin"
  [spiffs]="$BUILD_DIR/spiffs.bin"
  [app_bin]="$BUILD_DIR/VictronSolarDisplayEsp.bin"
)

mkdir -p "$RELEASE_DIR"

echo "Collecting release artifacts to: $RELEASE_DIR"

missing=()
for key in "${!artifacts[@]}"; do
  src="${artifacts[$key]}"
  if [ -f "$src" ]; then
    cp -v -- "$src" "$RELEASE_DIR/"
  else
    missing+=("$src")
  fi
done

if [ ${#missing[@]} -ne 0 ]; then
  echo "\nWarning: some artifacts were not found:" >&2
  for m in "${missing[@]}"; do
    echo "  - $m" >&2
  done
  echo "Release directory created but incomplete." >&2
  exit 2
fi

echo "\nAll artifacts copied successfully. Release contents:"
ls -l -- "$RELEASE_DIR"

exit 0
