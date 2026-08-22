@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 ( echo VCVARS_FAILED & exit /b 1 )
cd /d "%~dp0"
cl /nologo /Iinclude\pivot /Fobin\test_pkbindings.obj tests\test_pkbindings.c
if errorlevel 1 ( echo TEST_COMPILE_FAILED & exit /b 1 )
link /nologo /machine:x86 /out:bin\test_pkbindings.exe bin\test_pkbindings.obj
if errorlevel 1 ( echo TEST_LINK_FAILED & exit /b 1 )
bin\test_pkbindings.exe
