@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package-native.ps1" -SingleExe %*
if errorlevel 1 pause
