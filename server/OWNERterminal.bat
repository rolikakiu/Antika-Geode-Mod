@echo off
title Geometry Extra Owner Terminal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0OWNERterminal.ps1"
echo.
pause
