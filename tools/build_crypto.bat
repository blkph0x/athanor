@echo off
REM Back-compat wrapper. Prefer tools\build.bat (DEC-0004).
call "%~dp0build.bat" %*
exit /b %ERRORLEVEL%
