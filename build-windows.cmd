@echo off
rem Wrapper for running the Windows build script from cmd.exe or Explorer.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" %*
set "BUILD_EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %BUILD_EXIT_CODE%
