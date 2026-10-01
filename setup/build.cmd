@echo off
rem Build ImageMatrix_Setup.exe: compile uninstaller first (embedded as
rem resource), then the installer. Keep this file ASCII-only (codepage).
cd /d "%~dp0"

echo [0/3] refresh payload from build output ...
if not exist payload mkdir payload
copy /y ..\build\imagematrix.exe payload\ >nul
copy /y ..\build\imagematrix-cli.exe payload\ >nul
copy /y ..\README.md payload\ >nul

echo [1/3] uninstaller ...
g++ -O2 -municode -mwindows -DUNICODE -D_UNICODE src\uninstaller.c ^
    -o payload\uninstall.exe -lshell32 -ladvapi32 -lshlwapi -static -s
if errorlevel 1 exit /b 1

echo [2/3] resources ...
windres installer.rc -O coff -o installer.res.o
if errorlevel 1 exit /b 1

echo [3/3] setup ...
g++ -O2 -municode -mwindows -DUNICODE -D_UNICODE src\installer.c installer.res.o ^
    -o ImageMatrix_Setup.exe -lole32 -luuid -lshell32 -ladvapi32 -static -s
if errorlevel 1 exit /b 1

echo done: %~dp0ImageMatrix_Setup.exe
