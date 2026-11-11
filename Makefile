# MONSTER GPU POWER - derleme (GNU make). Linux'ta çapraz derleme: make  |  MSYS2 mingw64 kabuğunda: make CROSS=
CROSS   ?= x86_64-w64-mingw32-
CXX     := $(CROSS)g++
WINDRES := $(CROSS)windres
B       := build

CORE_SRC := $(wildcard src/core/*.cpp)
WIN_SRC  := $(wildcard src/win/*.cpp)

COMMON  := -std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -MMD -MP
LDFLAGS := -static -static-libgcc -static-libstdc++ -mwindows -municode -s \
           -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va -Wl,--no-insert-timestamp
LIBS    := -lsetupapi -lcfgmgr32 -lole32 -loleaut32 -luuid -lshell32 -ladvapi32 -luser32 -lgdi32 -lversion -lbcrypt

PROD_OBJ := $(patsubst src/%.cpp,$(B)/prod/%.o,$(CORE_SRC) $(WIN_SRC)) $(B)/prod/app_res.o
DEMO_OBJ := $(patsubst src/%.cpp,$(B)/demo/%.o,$(CORE_SRC) $(WIN_SRC)) $(B)/demo/app_res.o

.PHONY: all exe demo test installer clean
-include $(PROD_OBJ:.o=.d) $(DEMO_OBJ:.o=.d)
all: exe demo test

exe: $(B)/MonsterGpuPower.exe
demo: $(B)/MonsterGpuPower_DEMO.exe

$(B)/MonsterGpuPower.exe: $(PROD_OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS) $(LIBS)

# DEMO yalnız arayüz önizlemesi / sınama içindir: SAHTE donanım kullanır, dağıtıma KONMAZ.
$(B)/MonsterGpuPower_DEMO.exe: $(DEMO_OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS) $(LIBS)

$(B)/prod/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON) -c $< -o $@

$(B)/demo/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON) -DMGP_DEMO -c $< -o $@

$(B)/prod/app_res.o $(B)/demo/app_res.o: res/app.rc res/app.ico res/app.manifest res/resource.h
	@mkdir -p $(dir $@)
	$(WINDRES) -I res -i res/app.rc -o $@

# Birim testleri (ana makinede yerel derleme; donanım gerektirmez)
test:
	@mkdir -p $(B)
	g++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o $(B)/mgp_tests tests/test_main.cpp $(CORE_SRC)
	$(B)/mgp_tests

installer: exe
	makensis -V2 installer/setup.nsi

clean:
	rm -rf $(B)
