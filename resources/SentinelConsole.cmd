@echo off
setlocal
set "ROOT=%LOCALAPPDATA%\Sentinel"
echo Starting Sentinel using:
echo   %ROOT%
echo.
"%~dp0bin\SentinelCli.exe" "%ROOT%" interactive
echo.
pause
