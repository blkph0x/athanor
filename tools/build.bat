@echo off
REM Windows native builder (DEC-0004). GCC/MinGW or clang.
REM Usage: tools\build.bat
REM        tools\build.bat aarch64-w64-mingw32-gcc
setlocal
cd /d "%~dp0\.."
set CC=gcc
if not "%~1"=="" set CC=%~1
echo CC=%CC%
%CC% -dumpmachine
if not exist tests mkdir tests
make test
exit /b %ERRORLEVEL%
