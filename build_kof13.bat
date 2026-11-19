@echo off
setlocal
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "SRC=%ROOT%\src\native"
set "DIST=%ROOT%\dist\kof13"
set "BIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x86"
set "CLANG=%ROOT%\build\llvm-mingw-20261020-ucrt-x86_64\bin\i686-w64-mingw32-clang.exe"
if not exist "%BIN%\cl.exe" if exist "%CLANG%" goto clang
if not exist "%BIN%\cl.exe" (
    echo Falta el compilador de 32 bits: %BIN%\cl.exe
    echo En Visual Studio Installer agrega "MSVC v143 - VS 2022 C++ x64/x86 build tools" y vuelve a ejecutar este archivo.
    exit /b 1
)
set "PATH=%BIN%;%PATH%"
set "OUT=%ROOT%\build\obj32"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%DIST%" mkdir "%DIST%"

"%BIN%\lib.exe" /nologo /machine:x86 /def:"%SRC%\kernel32.def" /out:"%OUT%\kernel32.lib"
if errorlevel 1 exit /b 1
"%BIN%\lib.exe" /nologo /machine:x86 /def:"%SRC%\user32.def" /out:"%OUT%\user32.lib"
if errorlevel 1 exit /b 1

set "CFLAGS=/nologo /c /TC /O2 /GS- /Gs999999 /Zl /W3 /wd4204 /I%SRC%"
"%BIN%\cl.exe" %CFLAGS% /Fo"%OUT%\crtstub.obj" "%SRC%\crtstub.c"
if errorlevel 1 exit /b 1

set "CORE=util selector boundary registry route ui uimodel layout physical app kof13_overlay kof13_dll"
for %%F in (%CORE%) do (
    "%BIN%\cl.exe" %CFLAGS% /Fo"%OUT%\%%F.obj" "%SRC%\%%F.c"
    if errorlevel 1 exit /b 1
)
set OBJS=
for %%F in (%CORE%) do call set OBJS=%%OBJS%% "%OUT%\%%F.obj"

"%BIN%\link.exe" /nologo /DLL /MACHINE:X86 /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:DllMain /DEF:"%SRC%\kofvs32.def" /OUT:"%DIST%\KofVsSelector.dll" %OBJS% "%OUT%\crtstub.obj" "%OUT%\kernel32.lib" "%OUT%\user32.lib"
if errorlevel 1 exit /b 1
echo KOF13 DLL OK
exit /b 0

:clang
if not exist "%DIST%" mkdir "%DIST%"
"%CLANG%" -shared -O2 -D_M_IX86=1 -I"%SRC%" -Wno-unknown-pragmas -Wno-pragma-pack -Wno-incompatible-library-redeclaration -Wl,--kill-at -o "%DIST%\KofVsSelector.dll" "%SRC%\util.c" "%SRC%\selector.c" "%SRC%\boundary.c" "%SRC%\registry.c" "%SRC%\route.c" "%SRC%\ui.c" "%SRC%\uimodel.c" "%SRC%\layout.c" "%SRC%\physical.c" "%SRC%\app.c" "%SRC%\kof13_overlay.c" "%SRC%\kof13_dll.c" -lkernel32 -luser32
if errorlevel 1 exit /b 1
echo KOF13 DLL OK
rem Steam Deck / Proton: game.exe imports XINPUT1_3 and DINPUT8, so these proxies load the selector.
"%CLANG%" -shared -O2 -D_M_IX86=1 -DXPROXY_NAME="\"xinput1_3.dll\"" -DXPROXY_SYS="L\"xinput1_4.dll\"" -I"%SRC%" -Wno-unknown-pragmas -Wno-incompatible-library-redeclaration -Wl,--kill-at -o "%DIST%\xinput1_3.dll" "%SRC%\xinput1_4_proxy.c" "%SRC%\modload.c" "%SRC%\util.c" "%SRC%\xinput1_3.def" -lkernel32
if errorlevel 1 exit /b 1
"%CLANG%" -shared -O2 -D_M_IX86=1 -I"%SRC%" -Wno-unknown-pragmas -Wno-incompatible-library-redeclaration -Wl,--kill-at -o "%DIST%\dinput8.dll" "%SRC%\dinput8_proxy.c" "%SRC%\modload.c" "%SRC%\util.c" "%SRC%\dinput8.def" -lkernel32
if errorlevel 1 exit /b 1
echo KOF13 LOADERS OK
exit /b 0
