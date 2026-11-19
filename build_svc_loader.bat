@echo off
setlocal
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "SRC=%ROOT%\src\native"
set "CLANG=%ROOT%\build\llvm-mingw-20261020-ucrt-x86_64\bin\i686-w64-mingw32-clang.exe"
set "OUT=%ROOT%\dist\svc"
if not exist "%OUT%" mkdir "%OUT%"
call "%ROOT%\build_kof13.bat"
if errorlevel 1 exit /b 1
"%CLANG%" -shared -O2 -D_M_IX86=1 -I"%SRC%" -Wno-unknown-pragmas -Wno-incompatible-library-redeclaration -Wl,--kill-at -o "%OUT%\xinput1_4.dll" "%SRC%\xinput1_4_proxy.c" "%SRC%\modload.c" "%SRC%\util.c" "%SRC%\xinput1_4.def" -lkernel32
if errorlevel 1 exit /b 1
copy /Y "%ROOT%\dist\kof13\KofVsSelector.dll" "%OUT%\KofVsSelector.dll" >nul
copy /Y "D:\KOF-SteamDeck\KOF13\kofvs.ini" "%OUT%\kofvs.ini" >nul
echo SVC LOADER OK
exit /b 0
