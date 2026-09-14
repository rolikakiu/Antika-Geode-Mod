@echo off
if /i not "%1"=="minimized" (
  start "" /min "%~f0" minimized
  exit /b
)
title Geometry Dive Server
cd /d "%~dp0"

netstat -ano | findstr ":3000 " | findstr "LISTENING" >nul
if %errorlevel%==0 (
  echo Server is already running on port 3000.
  echo Opening the game...
  start "" http://localhost:3000/geometry-dash.html
  timeout /t 3 /nobreak >nul
  exit /b
)

echo Starting Geometry Dive server...
node server.js
pause