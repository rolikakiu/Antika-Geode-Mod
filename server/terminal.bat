@echo off
title Geometry Extra Terminal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0terminal.ps1"
echo.
pause