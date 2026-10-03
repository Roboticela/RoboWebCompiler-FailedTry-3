@echo off
setlocal
cd /d "%~dp0"
if exist C:\MinGW\bin set PATH=C:\MinGW\bin;%PATH%

if /I "%~1"=="build" goto build
if /I "%~1"=="run" goto run

echo Usage:
echo   RoboWeb.bat build
echo   RoboWeb.bat run Page.html [Style.css] [width] [height]
echo   The run command compiles Page.html to Build\Page.exe and opens that app.
echo   A linked stylesheet or script is compiled with the page. Style.css adds another stylesheet.
exit /b 1

:build
mingw32-make -f Makefile
exit /b %ERRORLEVEL%

:run
if "%~2"=="" goto missing
mingw32-make -f Makefile
if errorlevel 1 exit /b 1
if not exist Build mkdir Build
if not exist Build\Objects mkdir Build\Objects
set WIDTH=960
set HEIGHT=640
set CSS=
if /I "%~x3"==".css" (
    set CSS=%~3
    if not "%~4"=="" set WIDTH=%~4
    if not "%~5"=="" set HEIGHT=%~5
) else (
    if not "%~3"=="" set WIDTH=%~3
    if not "%~4"=="" set HEIGHT=%~4
)
set PAGE=Build\%~n2.exe
set CSSARG=
if not "%CSS%"=="" set CSSARG=--css "%CSS%"
Build\RoboWebCompiler.exe "%~2" -o "%PAGE%" --source-dir "%~dp0Source" --object-dir "%~dp0Build\Objects" --width %WIDTH% --height %HEIGHT% %CSSARG%
if errorlevel 1 exit /b 1
"%PAGE%" --width %WIDTH% --height %HEIGHT%
exit /b %ERRORLEVEL%

:missing
echo Usage: RoboWeb.bat run Page.html [Style.css] [width] [height]
exit /b 1
