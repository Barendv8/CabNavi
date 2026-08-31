@echo off
setlocal enabledelayedexpansion
title CabNavi installer

echo.
echo   CabNavi - installer
echo   ===================
echo.

rem ---------------------------------------------------------------------------
rem  Check that the files we need are actually next to this script. Running the
rem  installer straight out of the ZIP viewer is a common mistake, and without
rem  this check it would fail later with a confusing message.
rem ---------------------------------------------------------------------------
if not exist "%~dp0cabnavi.dll" (
    echo   ERROR: cabnavi.dll was not found next to this script.
    echo.
    echo   Extract the whole ZIP to a folder first, then run install.bat
    echo   from inside that folder.
    echo.
    pause
    exit /b 1
)

rem ---------------------------------------------------------------------------
rem  Find the game. We look in the usual Steam locations; if none of them hit,
rem  we ask. No guessing: a wrong folder means the plugin silently never loads.
rem ---------------------------------------------------------------------------
set "GAME="

for %%D in (
    "%ProgramFiles(x86)%\Steam\steamapps\common\Euro Truck Simulator 2"
    "%ProgramFiles%\Steam\steamapps\common\Euro Truck Simulator 2"
    "C:\Steam\steamapps\common\Euro Truck Simulator 2"
    "D:\Steam\steamapps\common\Euro Truck Simulator 2"
    "D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2"
    "E:\SteamLibrary\steamapps\common\Euro Truck Simulator 2"
) do (
    if not defined GAME if exist "%%~D\bin\win_x64\eurotrucks2.exe" set "GAME=%%~D"
)

if not defined GAME (
    echo   Could not find Euro Truck Simulator 2 automatically.
    echo.
    echo   Enter the game folder, for example:
    echo   C:\Program Files ^(x86^)\Steam\steamapps\common\Euro Truck Simulator 2
    echo.
    set /p "GAME=Game folder: "
)

if not exist "!GAME!\bin\win_x64\eurotrucks2.exe" (
    echo.
    echo   ERROR: no eurotrucks2.exe found in
    echo   !GAME!\bin\win_x64\
    echo.
    echo   That does not look like the game folder. Nothing was changed.
    echo.
    pause
    exit /b 1
)

echo   Game found:
echo   !GAME!
echo.

rem ---------------------------------------------------------------------------
rem  Copy the plugin.
rem ---------------------------------------------------------------------------
set "PLUGINS=!GAME!\bin\win_x64\plugins"
if not exist "!PLUGINS!" mkdir "!PLUGINS!"

copy /y "%~dp0cabnavi.dll" "!PLUGINS!\cabnavi.dll" >nul
if errorlevel 1 (
    echo   ERROR: could not copy the plugin.
    echo   Try running this installer as administrator.
    echo.
    pause
    exit /b 1
)
echo   [ok] plugin installed

rem ---------------------------------------------------------------------------
rem  Copy the icons. Without them the overlay still works - it falls back to
rem  simple drawn icons - so this is not fatal.
rem ---------------------------------------------------------------------------
set "DATA=%APPDATA%\CabNavi"
if not exist "!DATA!" mkdir "!DATA!"

if exist "%~dp0icons" (
    xcopy /e /i /y /q "%~dp0icons" "!DATA!\icons" >nul
    echo   [ok] icons installed
) else (
    echo   [--] no icons folder found, the overlay will draw its own
)

rem  Default logo, only if the user does not already have one. Never
rem  overwrite: someone who put their own company logo there should keep it.
if exist "%~dp0logo.png" (
    if not exist "!DATA!\logo.png" (
        copy /y "%~dp0logo.png" "!DATA!\logo.png" >nul
        echo   [ok] default logo installed
    ) else (
        echo   [--] you already have a logo.png, keeping it
    )
)

echo.
echo   Done.
echo.
echo   Start the game through TruckersMP.
echo   Press Insert to show or hide the overlay.
echo   Right click to toggle the mouse.
echo.
echo   Your own logo: put a PNG called logo.png in
echo   !DATA!
echo   It is drawn 32 pixels high, so around 64 pixels high works best.
echo.
pause
