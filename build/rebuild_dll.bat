@echo off
echo ========================================
echo  Step 1: Copy source files to lc-src
echo ========================================
copy /Y "c:\Projects\aardio\看穿gui\build\cimbar_dll.h" "C:\Projects\lc-src\src\lib\cimbar_dll\cimbar_dll.h"
copy /Y "c:\Projects\aardio\看穿gui\build\cimbar_dll.cpp" "C:\Projects\lc-src\src\lib\cimbar_dll\cimbar_dll.cpp"
echo.

echo ========================================
echo  Step 2: Rebuild DLL
echo ========================================
C:\Tools\msys64\msys2_shell.cmd -mingw32 -defterm -no-start -c "cd /c/Projects/lc-build && mingw32-make -j4 2>&1"
echo.

echo ========================================
echo  Step 3: Copy DLL to project
echo ========================================
copy /Y "C:\Projects\lc-build\build\src\lib\cimbar_dll\libcimbar_dll.dll" "c:\Projects\aardio\看穿gui\dll\cimbar_dll.dll"
echo.

echo Done! Press any key to exit...
pause >nul
