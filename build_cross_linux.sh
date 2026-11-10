#!/usr/bin/env bash
# Linux'ta çapraz derleme (mingw-w64, NSIS, Python3+pefile gerekir):  sudo apt-get install mingw-w64 nsis make python3-pefile
set -euo pipefail
cd "$(dirname "$0")"
make -j"$(nproc)" all            # üretim EXE + DEMO EXE + birim testleri (yerel g++ ile)
python3 tools/verify_pe.py build/MonsterGpuPower.exe
python3 tools/verify_pe.py build/MonsterGpuPower_DEMO.exe --demo || true
make installer                   # build/MONSTER_GPU_POWER_Setup.exe
sha256sum build/MonsterGpuPower.exe build/MONSTER_GPU_POWER_Setup.exe
