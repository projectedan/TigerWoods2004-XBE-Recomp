@echo off
rem Runs the game with M frame captures enabled. Captures go to captures\ beside this file.
set "XBE_CAPTURE_DIR=%~dp0captures"
rem frame timing in the log once a second
set XBE_PERF=1
set TW_FRAMES=1
if not exist "%XBE_CAPTURE_DIR%" mkdir "%XBE_CAPTURE_DIR%"
cd /d "%~dp0out\build\release"
start "" TW2004.exe
