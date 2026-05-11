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

PROJECT_NAME="$(basename "$(pwd)")"
VERSION="$(python3 - <<'PY'
import re
text = open("VERSION.md", "r", encoding="utf-8").read()
m = re.search(r"当前版本：`([^`]+)`", text)
if not m:
    raise SystemExit("VERSION not found")
print(m.group(1))
PY
)"
PACKAGE_DIR="${OUT_DIR}/${PROJECT_NAME}-${VERSION}"
ZIP_NAME="${OUT_DIR}/${PROJECT_NAME}-${VERSION}.zip"

# 清理旧的打包产物（zip 与目录）
rm -f "${OUT_DIR}/${PROJECT_NAME}-"*.zip
rm -rf "${OUT_DIR}/${PROJECT_NAME}-"*/

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

x86_64-w64-mingw32-g++ \
  -std=c++17 \
  -O2 \
  -I"${SDL2_DIR}/include" \
  src/retroid_sender.cpp \
  -L"${SDL2_DIR}/lib" \
  -lmingw32 -lSDL2main -lSDL2 -lws2_32 -mconsole \
  -o "${OUT_DIR}/retroid_sender.exe"

echo "已生成 ${OUT_DIR}/retroid_sender.exe"

# 更新打包目录（解压即用）
rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}"
cp "${OUT_DIR}/joystick_sender.exe" \
   "${OUT_DIR}/keyboard_sender.exe" \
   "${OUT_DIR}/udp_receiver.exe" \
   "${OUT_DIR}/retroid_sender.exe" \
   "${PACKAGE_DIR}/"

# 依赖文件与说明（从现有打包目录拷贝）
cp "dist/package/SDL2.dll" \
   "dist/package/libgcc_s_seh-1.dll" \
   "dist/package/libstdc++-6.dll" \
   "dist/package/libwinpthread-1.dll" \
   "dist/package/config.txt" \
   "dist/package/README.txt" \
   "${PACKAGE_DIR}/"

# 打包为 zip（解压即用）
export ZIP_NAME
export PACKAGE_DIR
python3 - <<'PY'
import os
import zipfile

zip_name = os.environ["ZIP_NAME"]
package_dir = os.environ["PACKAGE_DIR"]

with zipfile.ZipFile(zip_name, "w", compression=zipfile.ZIP_DEFLATED) as zf:
    for root, _, files in os.walk(package_dir):
        for fname in files:
            path = os.path.join(root, fname)
            arcname = os.path.relpath(path, os.path.dirname(package_dir))
            zf.write(path, arcname)
print(f"已生成 {zip_name}")
PY
echo "已生成 ${OUT_DIR}/joystick_sender.exe"
