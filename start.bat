@echo off
if not exist "%~dp0build\native\bin\CADWorkbench.exe" (
    echo Native application is not built. Run build.bat first.
    pause
    exit /b 1
)
start "CAD Workbench" "%~dp0build\native\bin\CADWorkbench.exe" %*
