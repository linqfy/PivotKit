@echo off
setlocal

set ROOT=%~dp0
set LUA=%ROOT%lua
set SRC=%ROOT%src
set OUT=%ROOT%bin

rem ---- Find MSVC (x86 toolchain). Override via VSINSTALLDIR if set. ----
set "VSROOT=%VSINSTALLDIR%"
if not defined VSROOT (
  if exist "D:\Program Files\Microsoft Visual Studio\2022\Community" set "VSROOT=D:\Program Files\Microsoft Visual Studio\2022\Community"
)
if not defined VSROOT (
  if exist "C:\Program Files\Microsoft Visual Studio\2022\Community" set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
)
if not defined VSROOT (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if exist "%VSWHERE%" (
    for /f "delims=" %%i in ('""%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath"') do set "VSROOT=%%i"
  )
)
if not defined VSROOT (
  echo [ERROR] Visual Studio with the C++ x86/x64 tools not found.
  echo         Install "Desktop development with C++" or set VSINSTALLDIR.
  exit /b 1
)

set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
  echo [ERROR] vcvarsall.bat not found under %VSROOT%
  exit /b 1
)
call "%VCVARS%" x86 >nul 2>&1
if errorlevel 1 (
  echo [ERROR] Could not load vcvarsall.bat x86
  exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\lua_obj" mkdir "%OUT%\lua_obj"

rem ---- Build Lua core into a static lib (32-bit) ----
cl /nologo /c /O2 /MT /D_CRT_SECURE_NO_WARNINGS /DLUA_BUILD_AS_DLL=0 /Fo"%OUT%\lua_obj\\" ^
   /I"%LUA%" ^
   "%LUA%\lapi.c" "%LUA%\lcode.c" "%LUA%\lctype.c" "%LUA%\ldebug.c" ^
   "%LUA%\ldo.c" "%LUA%\ldump.c" "%LUA%\lfunc.c" "%LUA%\lgc.c" ^
   "%LUA%\llex.c" "%LUA%\lmem.c" "%LUA%\lobject.c" "%LUA%\lopcodes.c" ^
   "%LUA%\lparser.c" "%LUA%\lstate.c" "%LUA%\lstring.c" "%LUA%\ltable.c" ^
   "%LUA%\ltm.c" "%LUA%\lundump.c" "%LUA%\lvm.c" "%LUA%\lzio.c" ^
   "%LUA%\lauxlib.c" "%LUA%\lbaselib.c" "%LUA%\lcorolib.c" "%LUA%\ldblib.c" ^
   "%LUA%\liolib.c" "%LUA%\lmathlib.c" "%LUA%\loadlib.c" "%LUA%\loslib.c" ^
   "%LUA%\lstrlib.c" "%LUA%\ltablib.c" "%LUA%\lutf8lib.c" "%LUA%\linit.c" >nul
if errorlevel 1 ( echo [ERROR] Lua compile failed & exit /b 1 )

lib /nologo /out:"%OUT%\lua54.lib" "%OUT%\lua_obj\*.obj" >nul
if errorlevel 1 ( echo [ERROR] lib failed & exit /b 1 )

rem ---- Build merged module set (pk_core/rtti/hooks/api) ----
for %%M in (pk_core pk_rtti pk_hooks pk_api) do (
  cl /nologo /c /O2 /MT /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 ^
     /I"%LUA%" /I"%SRC%" /I"%ROOT%include\pivot" /Fo"%OUT%\\\\" "%SRC%\%%M.c" >nul
  if errorlevel 1 ( echo [ERROR] %%M.c compile failed & exit /b 1 )
)

rem ---- Build host DLL (32-bit pivotkit.dll) ----
cl /nologo /c /O2 /MT /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 ^
   /I"%LUA%" /I"%SRC%" /I"%ROOT%include\pivot" /Fo"%OUT%\\\\" "%SRC%\pivotkit.cpp" >nul
if errorlevel 1 ( echo [ERROR] pivotkit.cpp compile failed & exit /b 1 )

link /nologo /dll /machine:x86 /out:"%OUT%\pivotkit.dll" ^
     "%OUT%\pivotkit.obj" "%OUT%\pk_core.obj" "%OUT%\pk_rtti.obj" ^
     "%OUT%\pk_hooks.obj" "%OUT%\pk_api.obj" "%OUT%\lua54.lib" ^
     kernel32.lib user32.lib advapi32.lib gdi32.lib gdiplus.lib ws2_32.lib >nul
if errorlevel 1 ( echo [ERROR] link pivotkit.dll failed & exit /b 1 )

rem ---- Build injector (32-bit pivotkit-loader.exe) ----
cl /nologo /c /O2 /MT /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 ^
   /I"%SRC%" /Fo"%OUT%\\" "%SRC%\injector.c" >nul
if errorlevel 1 ( echo [ERROR] injector.c compile failed & exit /b 1 )

link /nologo /machine:x86 /out:"%OUT%\pivotkit-loader.exe" ^
     "%OUT%\injector.obj" kernel32.lib user32.lib >nul
if errorlevel 1 ( echo [ERROR] link loader failed & exit /b 1 )

echo.
echo [OK] Built:
echo   %OUT%\pivotkit-loader.exe
echo   %OUT%\pivotkit.dll
endlocal
