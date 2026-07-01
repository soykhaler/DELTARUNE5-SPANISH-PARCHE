#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$ROOT/build/windows"
python3 "$ROOT/tools/embed_assets.py" --root "$ROOT"

CROSS_CC="${CROSS_CC:-x86_64-w64-mingw32-gcc}"
if ! command -v "$CROSS_CC" >/dev/null 2>&1; then
  echo "No encuentro $CROSS_CC. Instala mingw-w64 o compila en Windows." >&2
  exit 1
fi

RAYLIB_WIN_DIR="${RAYLIB_WIN_DIR:-$ROOT/third_party/raylib-5.5_win64_mingw-w64/raylib-5.5_win64_mingw-w64}"
if [ ! -f "$RAYLIB_WIN_DIR/include/raylib.h" ] || [ ! -f "$RAYLIB_WIN_DIR/lib/libraylib.a" ]; then
  echo "No encuentro raylib Windows en: $RAYLIB_WIN_DIR" >&2
  echo "Descarga raylib-5.5_win64_mingw-w64.zip y descomprimelo en third_party/." >&2
  exit 1
fi

"$CROSS_CC" -std=c99 -O2 -Wall -Wextra -mwindows -static -static-libgcc \
  "$ROOT/src/main.c" "$ROOT/src/embedded_assets.c" -o "$ROOT/build/windows/deltarune-es-patcher.exe" \
  -I"$RAYLIB_WIN_DIR/include" -L"$RAYLIB_WIN_DIR/lib" \
  -lraylib -lopengl32 -lgdi32 -lwinmm -lshell32 -lole32 -luuid -lcomdlg32

echo "Build Windows: $ROOT/build/windows/deltarune-es-patcher.exe"
