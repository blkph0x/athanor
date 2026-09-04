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
%CC% -std=c99 -Wall -Wextra -Werror -O2 -Iinclude -Itests -o tests\test_crypto.exe src\crypto\atn_platform.c src\crypto\atn_secure.c src\crypto\atn_sha256.c src\crypto\atn_sha512.c src\crypto\atn_hmac.c src\crypto\atn_hkdf.c src\crypto\atn_fips202.c src\crypto\atn_mlkem.c src\crypto\atn_chacha20.c src\crypto\atn_poly1305.c src\crypto\atn_aead.c src\crypto\atn_nonce.c tests\test_crypto.c -lbcrypt
if errorlevel 1 exit /b 1
tests\test_crypto.exe
exit /b %ERRORLEVEL%
