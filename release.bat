@echo off
set "GAME_EXE=TW2004"
cd /d "%~dp0"
set "OUT=out\build\release"
if not exist "%OUT%\%GAME_EXE%.exe" call build.bat || exit /b 1
rem the configure step writes where the SDK is
if not exist "%OUT%\sdk_dir.txt" cmake --preset release >nul || exit /b 1
if not exist "%OUT%\sdk_dir.txt" echo %OUT%\sdk_dir.txt was not written, update the SDK. && exit /b 1
set /p SDK=<out\build\release\sdk_dir.txt
rem 7-Zip on the PATH, else where its installer puts it
set "SEVENZIP=7z"
where 7z >nul 2>nul || set "SEVENZIP=%ProgramFiles%\7-Zip\7z.exe"
if not "%SEVENZIP%"=="7z" if not exist "%SEVENZIP%" (echo 7-Zip not found: install it from 7-zip.org & exit /b 1)
del /q "%GAME_EXE%.zip" 2>nul
rem an empty assets folder beside the exe, where the game (files or .iso) goes
rd /s /q "%OUT%\release" 2>nul
mkdir "%OUT%\release\assets"
rem the compiler folder without its work folder: no game code goes in the zip, the first start builds it
"%SEVENZIP%" a "%GAME_EXE%.zip" ".\%OUT%\%GAME_EXE%.exe" ".\%OUT%\release\assets" ".\%OUT%\compiler" .\README.md .\LICENSE "%SDK%\THIRD_PARTY.txt" "-xr!compiler\work" || exit /b 1
