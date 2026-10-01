@echo off
rem ============================================================
rem  ImageMatrix build script (MinGW-w64 g++)
rem  Produces: build\imagematrix.exe      (GUI)
rem            build\imagematrix-cli.exe  (console / self-test)
rem
rem  NOTE: this file is ASCII only on purpose - cmd.exe parses
rem        batch files using the OEM code page, so Chinese text
rem        here would corrupt the script.
rem ============================================================
setlocal
cd /d "%~dp0"

set CXX=g++
where %CXX% >nul 2>nul
if errorlevel 1 (
  echo [ERROR] g++ not found in PATH. Install MinGW-w64 and add it to PATH.
  exit /b 1
)

set FLAGS=-std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -Isrc -finput-charset=UTF-8
set LIBS=-static -lgdiplus -lcomctl32 -lcomdlg32 -lgdi32 -lole32 -luuid -lshell32 -luser32

if not exist build mkdir build

echo [1/4] compiling core modules...
%CXX% %FLAGS% -c src\image.cpp      -o build\image.o      || goto :err
%CXX% %FLAGS% -c src\bmp.cpp        -o build\bmp.o        || goto :err
%CXX% %FLAGS% -c src\gdiplus_io.cpp -o build\gdiplus_io.o || goto :err
%CXX% %FLAGS% -c src\matrix.cpp     -o build\matrix.o     || goto :err
%CXX% %FLAGS% -c src\gui.cpp        -o build\gui.o        || goto :err

echo [2/4] compiling entry points...
%CXX% %FLAGS% -DIMAGEMATRIX_GUI -c src\main.cpp -o build\main_gui.o || goto :err
%CXX% %FLAGS% -c src\main.cpp -o build\main_cli.o || goto :err

echo [3/4] compiling resources...
set RES=
where windres >nul 2>nul
if errorlevel 1 goto :nores
windres -I res res\app.rc -O coff -o build\app.res.o || goto :err
set RES=build\app.res.o
:nores

echo [4/4] linking...
%CXX% -municode -mwindows -o build\imagematrix.exe build\image.o build\bmp.o build\gdiplus_io.o build\matrix.o build\gui.o build\main_gui.o %RES% %LIBS% || goto :err
%CXX% -o build\imagematrix-cli.exe build\image.o build\bmp.o build\gdiplus_io.o build\matrix.o build\gui.o build\main_cli.o %LIBS% || goto :err

echo.
echo Build OK:
echo   build\imagematrix.exe      - GUI version (double click to run)
echo   build\imagematrix-cli.exe  - console version (run "selftest" to verify)
exit /b 0

:err
echo.
echo [ERROR] build failed.
exit /b 1
