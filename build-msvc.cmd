@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-msvc.ps1" %*
