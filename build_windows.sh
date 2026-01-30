#!/usr/bin/env bash
set -euo pipefail

# 需要提前设置 SDL2_DIR 指向解压后的 SDL2 开发包（包含 include/ 和 lib/）
# 例如：export SDL2_DIR="$PWD/deps/SDL2-2.30.2/x86_64-w64-mingw32"

if [[ -z "${SDL2_DIR:-}" ]]; then
  echo "SDL2_DIR 未设置，请先 export SDL2_DIR=..."
  exit 1
fi

OUT_DIR="${1:-dist}"
mkdir -p "${OUT_DIR}"

x86_64-w64-mingw32-g++ \
  -std=c++17 \
  -O2 \
  -I"${SDL2_DIR}/include" \
  src/main.cpp \
  -L"${SDL2_DIR}/lib" \
  -lmingw32 -lSDL2main -lSDL2 -lws2_32 -mconsole \
  -o "${OUT_DIR}/joystick_sender.exe"

echo "已生成 ${OUT_DIR}/joystick_sender.exe"

x86_64-w64-mingw32-g++ \
  -std=c++17 \
  -O2 \
  src/udp_receiver.cpp \
  -lws2_32 -mconsole \
  -o "${OUT_DIR}/udp_receiver.exe"

echo "已生成 ${OUT_DIR}/udp_receiver.exe"

x86_64-w64-mingw32-g++ \
  -std=c++17 \
  -O2 \
  src/keyboard_sender.cpp \
  -lws2_32 -mconsole \
  -o "${OUT_DIR}/keyboard_sender.exe"

echo "已生成 ${OUT_DIR}/keyboard_sender.exe"
echo "已生成 ${OUT_DIR}/joystick_sender.exe"
