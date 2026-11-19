@echo off
setlocal
set "BIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"
set "PATH=%BIN%;%PATH%"
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "SRC=%ROOT%\src\native"
set "OUT=%ROOT%\build"
set "DIST=%ROOT%\dist"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
if not exist "%DIST%" mkdir "%DIST%"

"%BIN%\lib.exe" /nologo /machine:x64 /def:"%SRC%\kernel32.def" /out:"%OUT%\kernel32.lib" >nul
if errorlevel 1 exit /b 1
"%BIN%\lib.exe" /nologo /machine:x64 /def:"%SRC%\user32.def" /out:"%OUT%\user32.lib" >nul
if errorlevel 1 exit /b 1

"%BIN%\cl.exe" /nologo /c /TC /Od /GS- /Zl /W3 /Fo"%OUT%\obj\crtstub.obj" "%SRC%\crtstub.c"
if errorlevel 1 exit /b 1
"%BIN%\ml64.exe" /nologo /c /Fo"%OUT%\obj\chkstk.obj" "%SRC%\chkstk.asm" >nul
if errorlevel 1 exit /b 1

set "CORE=util selector boundary registry route ui uimodel layout"
set "RUNTIME=physical overlay hook app iat png"
for %%F in (%CORE% %RUNTIME% selftest proto dllmain) do (
    "%BIN%\cl.exe" /nologo /c /TC /O2 /GS- /Zl /W4 /wd4204 /I"%SRC%" /Fo"%OUT%\obj\%%F.obj" "%SRC%\%%F.c"
    if errorlevel 1 exit /b 1
)
set CORE_OBJS=
for %%F in (%CORE%) do call set CORE_OBJS=%%CORE_OBJS%% "%OUT%\obj\%%F.obj"
set RUNTIME_OBJS=
for %%F in (%RUNTIME%) do call set RUNTIME_OBJS=%%RUNTIME_OBJS%% "%OUT%\obj\%%F.obj"
set RT="%OUT%\obj\crtstub.obj" "%OUT%\obj\chkstk.obj" "%OUT%\kernel32.lib"

"%BIN%\link.exe" /nologo /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:Main /OUT:"%OUT%\SelectorTests.exe" "%OUT%\obj\selftest.obj" "%OUT%\obj\png.obj" "%OUT%\obj\hook.obj" "%OUT%\obj\physical.obj" %CORE_OBJS% %RT%
if errorlevel 1 exit /b 1
"%BIN%\link.exe" /nologo /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:Main /OUT:"%DIST%\SelectorPrototype.exe" "%OUT%\obj\proto.obj" %RUNTIME_OBJS% %CORE_OBJS% %RT% "%OUT%\user32.lib"
if errorlevel 1 exit /b 1
"%BIN%\link.exe" /nologo /DLL /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:DllMain /OUT:"%DIST%\KofVsSelector.dll" "%OUT%\obj\dllmain.obj" %RUNTIME_OBJS% %CORE_OBJS% %RT% "%OUT%\user32.lib"
if errorlevel 1 exit /b 1
"%BIN%\cl.exe" /nologo /c /TC /O2 /GS- /Zl /W4 /wd4204 /I"%SRC%" /Fo"%OUT%\obj\modload.obj" "%SRC%\modload.c"
if errorlevel 1 exit /b 1
"%BIN%\cl.exe" /nologo /c /TC /O2 /GS- /Zl /W4 /wd4204 /I"%SRC%" /Fo"%OUT%\obj\dinput8_proxy.obj" "%SRC%\dinput8_proxy.c"
if errorlevel 1 exit /b 1
"%BIN%\link.exe" /nologo /DLL /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:DllMain /DEF:"%SRC%\dinput8.def" /OUT:"%DIST%\dinput8.dll" "%OUT%\obj\dinput8_proxy.obj" "%OUT%\obj\modload.obj" "%OUT%\obj\util.obj" %RT%
if errorlevel 1 exit /b 1
"%BIN%\cl.exe" /nologo /c /TC /O2 /GS- /Zl /W4 /wd4204 /I"%SRC%" /Fo"%OUT%\obj\xinput1_4_proxy.obj" "%SRC%\xinput1_4_proxy.c"
if errorlevel 1 exit /b 1
"%BIN%\link.exe" /nologo /DLL /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:DllMain /DEF:"%SRC%\xinput1_4.def" /OUT:"%DIST%\xinput1_4.dll" "%OUT%\obj\xinput1_4_proxy.obj" "%OUT%\obj\modload.obj" "%OUT%\obj\util.obj" %RT%
if errorlevel 1 exit /b 1

if "%1"=="notest" exit /b 0
"%OUT%\SelectorTests.exe"
exit /b %ERRORLEVEL%
