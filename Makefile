# Makefile -- ImageMatrix (MinGW-w64 / g++ 或 MSYS2 环境均可)
#   mingw32-make            构建 GUI 版 + 命令行版
#   mingw32-make gui        只构建 GUI 版
#   mingw32-make clean      清理
#
# 说明：Windwos 下 make 命令名通常是 mingw32-make。

CXX      ?= g++
STD      ?= -std=c++17
OPT      ?= -O2
WARN     ?= -Wall -Wextra
DEFS     := -DUNICODE -D_UNICODE
INC      := -Isrc
SRCCHARS := -finput-charset=UTF-8
CXXFLAGS ?= $(STD) $(OPT) $(WARN) $(DEFS) $(INC) $(SRCCHARS)

BUILD    := build
# -static 让 exe 自带 C++ 运行库，避免机器上其它软件(如 Inkscape/其它 MinGW 程序)的
# libstdc++-6.dll 抢先加载导致崩溃，同时分发时无需附带任何 DLL。
LIBS     := -static -lgdiplus -lcomctl32 -lcomdlg32 -lgdi32 -lole32 -luuid -lshell32 -luser32

COMMON_SRC := src/image.cpp src/bmp.cpp src/gdiplus_io.cpp src/matrix.cpp src/gui.cpp
COMMON_OBJ := $(patsubst src/%.cpp,$(BUILD)/%.o,$(COMMON_SRC))

GUI_EXE := $(BUILD)/imagematrix.exe
CLI_EXE := $(BUILD)/imagematrix-cli.exe

all: gui cli

gui: $(GUI_EXE)
cli: $(CLI_EXE)

$(BUILD):
	-mkdir $(BUILD)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/main_gui.o: src/main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -DIMAGEMATRIX_GUI -c $< -o $@

$(BUILD)/main_cli.o: src/main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/app.res.o: res/app.rc res/app.manifest | $(BUILD)
	windres -I res res/app.rc -O coff -o $@

$(GUI_EXE): $(COMMON_OBJ) $(BUILD)/main_gui.o $(BUILD)/app.res.o
	$(CXX) -municode -mwindows -o $@ $^ $(LIBS)
	@echo   built: $@

$(CLI_EXE): $(COMMON_OBJ) $(BUILD)/main_cli.o
	$(CXX) -o $@ $^ $(LIBS)
	@echo   built: $@

selftest: $(CLI_EXE)
	$(CLI_EXE) selftest -d $(BUILD)/selftest_out

clean:
	-rmdir /s /q $(BUILD) 2>nul
	-del /q $(BUILD)\*.o 2>nul

.PHONY: all gui cli selftest clean
