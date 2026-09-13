@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-qt-msvc.ps1" %*
