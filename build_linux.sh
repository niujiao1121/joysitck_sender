#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${ROOT_DIR}/dist/linux"

mkdir -p "${OUT_DIR}"

g++ -std=c++17 -O2 \
  "${ROOT_DIR}/src/joystick_sender_linux.cpp" \
  -o "${OUT_DIR}/joystick_sender_linux" \
  $(pkg-config --cflags --libs sdl2) \
  -static-libstdc++ -static-libgcc

chmod +x "${OUT_DIR}/joystick_sender_linux"
echo "已生成 ${OUT_DIR}/joystick_sender_linux"
