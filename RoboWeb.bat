@echo off
setlocal
cd /d "%~dp0"
if exist C:\MinGW\bin set PATH=C:\MinGW\bin;%PATH%

if /I "%~1"=="build" goto build
if /I "%~1"=="run" goto run

echo Usage:
echo   RoboWeb.bat build
echo   RoboWeb.bat run Page.html [width] [height]
exit /b 1

:build
mingw32-make -f Makefile
exit /b %ERRORLEVEL%

:run
if "%~2"=="" goto missing
mingw32-make -f Makefile
if errorlevel 1 exit /b 1
if not exist Build mkdir Build
set WIDTH=960
set HEIGHT=640
if not "%~3"=="" set WIDTH=%~3
if not "%~4"=="" set HEIGHT=%~4
python "%~dp0Tools\Viewer.py" "%~2" %WIDTH% %HEIGHT%
exit /b %ERRORLEVEL%

:missing
echo Usage: RoboWeb.bat run Page.html [width] [height]
exit /b 1
