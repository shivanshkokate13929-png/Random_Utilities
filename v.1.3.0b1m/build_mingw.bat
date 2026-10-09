@echo off
REM MinGW-w64 / MSYS2 build. -mwindows = no stray console window behind the GUI.
cd /d "%~dp0"
g++ -std=c++17 -O2 -mwindows RandomUtilities.cpp RandomUtilitiesGUI.cpp -o RandomUtilities.exe -lgdiplus -lgdi32 -lpsapi -lole32 -luuid
if errorlevel 1 goto failed
echo Built RandomUtilities.exe
goto done
:failed
echo BUILD FAILED
:done
pause
