#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$ROOT/build/windows"

CROSS_CC="${CROSS_CC:-x86_64-w64-mingw32-gcc}"
if ! command -v "$CROSS_CC" >/dev/null 2>&1; then
  echo "No encuentro $CROSS_CC. Instala mingw-w64 o compila en Windows." >&2
  exit 1
fi

if [ ! -f /usr/x86_64-w64-mingw32/include/raylib.h ]; then
  echo "No encuentro raylib para Windows en /usr/x86_64-w64-mingw32." >&2
  echo "Instala/copiala ahi o compila este proyecto desde Windows con raylib instalado." >&2
  exit 1
fi

"$CROSS_CC" -std=c99 -O2 -Wall -Wextra "$ROOT/src/main.c" -o "$ROOT/build/windows/deltarune-es-patcher.exe" \
  -I/usr/x86_64-w64-mingw32/include -L/usr/x86_64-w64-mingw32/lib \
  -lraylib -lopengl32 -lgdi32 -lwinmm -lshell32

echo "Build Windows: $ROOT/build/windows/deltarune-es-patcher.exe"
