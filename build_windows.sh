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
GXX=x86_64-w64-mingw32-g++

# 清理旧的打包产物（zip 与目录）
rm -f "${OUT_DIR}/${PROJECT_NAME}-"*.zip
rm -rf "${OUT_DIR}/${PROJECT_NAME}-"*/

"${GXX}" \
  -std=c++17 \
  -O2 \
  -I"${SDL2_DIR}/include" \
  src/main.cpp \
  -L"${SDL2_DIR}/lib" \
  -lmingw32 -lSDL2main -lSDL2 -lws2_32 -mconsole \
  -o "${OUT_DIR}/joystick_sender.exe"

echo "已生成 ${OUT_DIR}/joystick_sender.exe"

"${GXX}" \
  -std=c++17 \
  -O2 \
  src/udp_receiver.cpp \
  -lws2_32 -mconsole \
  -o "${OUT_DIR}/udp_receiver.exe"

echo "已生成 ${OUT_DIR}/udp_receiver.exe"

"${GXX}" \
  -std=c++17 \
  -O2 \
  src/keyboard_sender.cpp \
  -lws2_32 -mconsole \
  -o "${OUT_DIR}/keyboard_sender.exe"

echo "已生成 ${OUT_DIR}/keyboard_sender.exe"

"${GXX}" \
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

copy_runtime_dll() {
  local name="$1"
  local path
  path="$("${GXX}" -print-file-name="${name}")"
  if [[ -f "${path}" ]]; then
    cp "${path}" "${PACKAGE_DIR}/"
    return
  fi
  path="/usr/x86_64-w64-mingw32/lib/${name}"
  if [[ -f "${path}" ]]; then
    cp "${path}" "${PACKAGE_DIR}/"
    return
  fi
  echo "找不到运行时依赖: ${name}"
  exit 1
}

cp "${SDL2_DIR}/bin/SDL2.dll" "${PACKAGE_DIR}/"
copy_runtime_dll "libgcc_s_seh-1.dll"
copy_runtime_dll "libstdc++-6.dll"
copy_runtime_dll "libwinpthread-1.dll"
cp "config.txt" "${PACKAGE_DIR}/"
cat > "${PACKAGE_DIR}/README.txt" <<'EOF'
joystick_sender Windows package

Run:
  joystick_sender.exe config.txt

Local UDP receiver test:
  udp_receiver.exe 12121

Keyboard sender:
  keyboard_sender.exe

Retroid sender:
  retroid_sender.exe
EOF

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
