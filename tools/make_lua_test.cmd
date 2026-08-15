@echo off
rem make_lua_test.cmd - builds bin\lua.exe (standalone interpreter, 32-bit)
rem from the vendored Lua source, so pivotlib can be tested without pivot.exe:
rem     bin\lua.exe tests\pivotlib_test.lua
setlocal

set ROOT=%~dp0..
set LUA=%ROOT%\lua
set OUT=%ROOT%\bin

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
  echo [ERROR] Visual Studio with C++ tools not found.
  exit /b 1
)

call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
if errorlevel 1 ( echo [ERROR] vcvarsall x86 failed & exit /b 1 )

if not exist "%OUT%\lua54.lib" (
  echo [ERROR] bin\lua54.lib not found. Run build.bat first.
  exit /b 1
)

cl /nologo /O2 /MT /D_CRT_SECURE_NO_WARNINGS ^
   /I"%LUA%" /Fo"%OUT%\\" /Fe"%OUT%\lua.exe" "%LUA%\lua.c" /link "%OUT%\lua54.lib" >nul
if errorlevel 1 ( echo [ERROR] lua.exe build failed & exit /b 1 )

echo [OK] Built %OUT%lua.exe
endlocal
