@echo off
rem Runs the game with M frame captures enabled. Captures go to captures\ beside this file.
set "XBE_CAPTURE_DIR=%~dp0captures"
if not exist "%XBE_CAPTURE_DIR%" mkdir "%XBE_CAPTURE_DIR%"
cd /d "%~dp0out\build\release"
start "" TW2004.exe
