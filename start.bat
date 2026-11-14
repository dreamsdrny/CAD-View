@echo off
if not exist "%~dp0build\native\bin\CAD.exe" (
    echo Native application is not built. Run build.bat first.
    pause
    exit /b 1
)
start "CAD" "%~dp0build\native\bin\CAD.exe" %*
