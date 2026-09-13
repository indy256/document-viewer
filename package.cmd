@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1"
exit /b %errorlevel%
