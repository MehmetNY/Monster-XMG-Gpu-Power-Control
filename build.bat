@echo off
rem MONSTER GPU POWER - Windows'ta derleme (MSYS2 MinGW-w64 g++ gerekir).
rem Kurulum: https://www.msys2.org  ->  pacman -S mingw-w64-x86_64-gcc  ->  C:\msys64\mingw64\bin yolunu PATH'e ekleyin.
rem Kullanim:  build.bat            (build\MonsterGpuPower.exe uretir)
rem            build.bat demo       (SAHTE donanimli arayuz onizleme build'i; dagitmayin)
setlocal EnableDelayedExpansion
where g++ >nul 2>&1 || (echo [HATA] g++ bulunamadi. MSYS2 MinGW-w64 kurup bin klasorunu PATH'e ekleyin. & exit /b 1)
where windres >nul 2>&1 || (echo [HATA] windres bulunamadi. & exit /b 1)
if not exist build mkdir build
set DEFS=-DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DNOMINMAX -DWIN32_LEAN_AND_MEAN
set OUT=build\MonsterGpuPower.exe
if /I "%1"=="demo" (set DEFS=!DEFS! -DMGP_DEMO & set OUT=build\MonsterGpuPower_DEMO.exe)
set SRC=
for %%f in (src\core\*.cpp src\win\*.cpp) do set SRC=!SRC! %%f
windres -I res -i res\app.rc -o build\app_res.o || exit /b 1
g++ -std=c++17 -O2 -Wall -Wextra !DEFS! !SRC! build\app_res.o -o !OUT! -static -static-libgcc -static-libstdc++ -mwindows -municode -s -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va -Wl,--no-insert-timestamp -lsetupapi -lcfgmgr32 -lole32 -loleaut32 -luuid -lshell32 -ladvapi32 -luser32 -lgdi32 -lversion -lbcrypt || exit /b 1
echo [TAMAM] !OUT!
rem Birim testleri (donanim gerektirmez):  g++ -std=c++17 -O1 -o build\mgp_tests.exe tests\test_main.cpp src\core\*.cpp ^&^& build\mgp_tests.exe
