#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$ROOT/build/linux"

if pkg-config --exists raylib; then
  cc -std=c99 -O2 -Wall -Wextra "$ROOT/src/main.c" -o "$ROOT/build/linux/deltarune-es-patcher" $(pkg-config --cflags --libs raylib) -lpthread
else
  cc -std=c99 -O2 -Wall -Wextra "$ROOT/src/main.c" -o "$ROOT/build/linux/deltarune-es-patcher" -lraylib -lm -lpthread -ldl -lrt -lX11
fi

echo "Build Linux: $ROOT/build/linux/deltarune-es-patcher"
