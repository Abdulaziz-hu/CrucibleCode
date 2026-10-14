@echo off
rem Wrapper so that the PowerShell script can be started with a double click or from cmd.exe.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" %*
exit /b %ERRORLEVEL%
