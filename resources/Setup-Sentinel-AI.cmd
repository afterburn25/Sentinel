@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0ai\Setup-Sentinel-AI.ps1"
exit /b %ERRORLEVEL%
