@echo off
setlocal
set "ROOT=%LOCALAPPDATA%\Sentinel"
echo Starting SARA console using preserved data root:
echo   %ROOT%
echo.
"%~dp0bin\SentinelCli.exe" "%ROOT%" interactive
echo.
pause
