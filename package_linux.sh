#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$ROOT/build_linux.sh"

BIN_DIR="$ROOT/release/linux/deltarune-es-raylib-patcher"
SRC_DIR="$ROOT/release/source/deltarune-es-raylib-patcher-source"

rm -rf "$ROOT/release/linux" "$ROOT/release/source"
rm -f "$ROOT/release/deltarune-es-raylib-patcher-linux.zip" \
      "$ROOT/release/deltarune-es-raylib-patcher-linux.zip.sha256" \
      "$ROOT/release/deltarune-es-raylib-patcher-source.zip" \
      "$ROOT/release/deltarune-es-raylib-patcher-source.zip.sha256"
mkdir -p "$BIN_DIR" "$SRC_DIR"

cp "$ROOT/build/linux/deltarune-es-patcher" "$BIN_DIR/"
cp "$ROOT/FINAL_README.txt" "$BIN_DIR/README.txt"
chmod +x "$BIN_DIR/deltarune-es-patcher"

cp -a "$ROOT/src" "$SRC_DIR/"
rm -f "$SRC_DIR/src/embedded_assets.c"
cp -a "$ROOT/assets" "$SRC_DIR/"
mkdir -p "$SRC_DIR/payload"
cp -a "$ROOT/payload/tables" "$SRC_DIR/payload/"
cp "$ROOT/payload/profiles.json" "$SRC_DIR/payload/"
cp -a "$ROOT/tools" "$SRC_DIR/"
cp "$ROOT/create_patch.sh" "$SRC_DIR/"
cp "$ROOT/build_linux.sh" "$SRC_DIR/"
cp "$ROOT/build_windows.sh" "$SRC_DIR/"
cp "$ROOT/package_linux.sh" "$SRC_DIR/"
cp "$ROOT/README.md" "$SRC_DIR/"

cd "$ROOT/release/linux"
zip -qr ../deltarune-es-raylib-patcher-linux.zip deltarune-es-raylib-patcher
cd "$ROOT/release"
sha256sum deltarune-es-raylib-patcher-linux.zip > deltarune-es-raylib-patcher-linux.zip.sha256

cd "$ROOT/release/source"
zip -qr ../deltarune-es-raylib-patcher-source.zip deltarune-es-raylib-patcher-source
cd "$ROOT/release"
sha256sum deltarune-es-raylib-patcher-source.zip > deltarune-es-raylib-patcher-source.zip.sha256

echo "Release Linux: $ROOT/release/deltarune-es-raylib-patcher-linux.zip"
echo "Source zip   : $ROOT/release/deltarune-es-raylib-patcher-source.zip"
