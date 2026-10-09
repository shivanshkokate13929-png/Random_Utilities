@echo off
REM Run from a "x64 Native Tools Command Prompt for VS". Untested - report any errors.
cl /nologo /EHsc /std:c++17 /O2 /DNOMINMAX RandomUtilities.cpp RandomUtilitiesGUI.cpp /Fe:RandomUtilities.exe /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup gdiplus.lib gdi32.lib user32.lib psapi.lib
