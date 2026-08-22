@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 ( echo VCVARS_FAILED & exit /b 1 )
cd /d "%~dp0"
cl /nologo /c /O2 /MT /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /Ilua /Isrc /Fobin\pk_core.obj src\pk_core.c
if errorlevel 1 ( echo MOD_CORE_FAILED & exit /b 1 )
cl /nologo /c /O2 /MT /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /Ilua /Isrc /Fobin\pk_rtti.obj src\pk_rtti.c
if errorlevel 1 ( echo MOD_RTTI_FAILED & exit /b 1 )
cl /nologo /c /O2 /MT /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /Ilua /Isrc /Fobin\pk_hooks.obj src\pk_hooks.c
if errorlevel 1 ( echo MOD_HOOKS_FAILED & exit /b 1 )
cl /nologo /c /O2 /MT /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /Ilua /Isrc /Iinclude\pivot /Fobin\pk_api.obj src\pk_api.c
if errorlevel 1 ( echo MOD_API_FAILED & exit /b 1 )
cl /nologo /c /O2 /MT /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /Ilua /Isrc /Iinclude\pivot /Fo:bin\pivotkit.obj src\pivotkit.cpp
if errorlevel 1 ( echo COMPILE_FAILED & exit /b 1 )
link /nologo /dll /machine:x86 /out:bin\pivotkit.dll bin\pivotkit.obj bin\pk_core.obj bin\pk_rtti.obj bin\pk_hooks.obj bin\pk_api.obj bin\lua54.lib kernel32.lib user32.lib advapi32.lib gdi32.lib gdiplus.lib ws2_32.lib
if errorlevel 1 ( echo LINK_FAILED & exit /b 1 )
echo BUILD_OK
