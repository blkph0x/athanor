@echo off
REM REQ-1.1 build. DEC-0001. GCC 11.3.0. Links bcrypt (OS CSPRNG), nothing else.
setlocal
cd /d "%~dp0\.."
if not exist tests mkdir tests
gcc -std=c99 -Wall -Wextra -Werror -O2 -Iinclude -o tests\test_crypto.exe src\crypto\atn_secure.c src\crypto\atn_sha256.c src\crypto\atn_hmac.c src\crypto\atn_hkdf.c src\crypto\atn_chacha20.c src\crypto\atn_poly1305.c src\crypto\atn_aead.c src\crypto\atn_nonce.c tests\test_crypto.c -lbcrypt
if errorlevel 1 exit /b 1
tests\test_crypto.exe
exit /b %ERRORLEVEL%
