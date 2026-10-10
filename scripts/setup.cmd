@echo off
rem CrucibleCode - Windows setup launcher (installs dependencies, builds, runs).
rem Usage: scripts\setup.cmd [-Config Debug] [-NoRun] [-Clean] [-WithDotNet] [-Repo <url>]
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
if errorlevel 1 (
  echo.
  echo Setup failed. See the messages above.
  pause
)
