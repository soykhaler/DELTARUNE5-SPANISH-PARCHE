#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$ROOT/build/linux"
python3 "$ROOT/tools/embed_assets.py" --root "$ROOT"

SOURCES=("$ROOT/src/main.c" "$ROOT/src/embedded_assets.c")

if [ -f /usr/local/lib/libraylib.a ]; then
  cc -std=c99 -O2 -Wall -Wextra "${SOURCES[@]}" \
    -o "$ROOT/build/linux/deltarune-es-patcher" \
    -I/usr/local/include /usr/local/lib/libraylib.a -lm -lpthread -ldl -lrt -lGL -lX11
elif pkg-config --exists raylib; then
  cc -std=c99 -O2 -Wall -Wextra "${SOURCES[@]}" \
    -o "$ROOT/build/linux/deltarune-es-patcher" \
    $(pkg-config --cflags --libs raylib) -lpthread
else
  cc -std=c99 -O2 -Wall -Wextra "${SOURCES[@]}" \
    -o "$ROOT/build/linux/deltarune-es-patcher" \
    -lraylib -lm -lpthread -ldl -lrt -lX11
fi

echo "Build Linux: $ROOT/build/linux/deltarune-es-patcher"
