@echo off
setlocal
title C2 Dummy Client Launcher
cd /d "%~dp0.."

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_dummy_clients_visible.ps1" -BinDir ".\x64\Release"
if errorlevel 1 pause
endlocal
