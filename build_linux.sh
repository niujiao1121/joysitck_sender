#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${ROOT_DIR}/dist/linux"

mkdir -p "${OUT_DIR}"

if ! command -v pkg-config >/dev/null 2>&1; then
  echo "缺少 pkg-config，请先安装：sudo apt-get install -y pkg-config"
  exit 1
fi

if ! pkg-config --exists sdl2; then
  echo "缺少 SDL2 开发包，请先安装：sudo apt-get install -y libsdl2-dev"
  exit 1
fi

g++ -std=c++17 -O2 \
  "${ROOT_DIR}/src/joystick_sender_linux.cpp" \
  -o "${OUT_DIR}/joystick_sender_linux" \
  $(pkg-config --cflags --libs sdl2) \
  -static-libstdc++ -static-libgcc

chmod +x "${OUT_DIR}/joystick_sender_linux"
cp "${ROOT_DIR}/config.txt" "${OUT_DIR}/config.txt"
echo "已生成 ${OUT_DIR}/joystick_sender_linux"
