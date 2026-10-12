#pragma once
#include <QString>

// Works out how to execute an arbitrary file (setup.sh, build.ps1, tool.py, app.exe, ...).
namespace RunHelper {

// True when CrucibleCode knows how to run this file.
bool isRunnable(const QString& path);

// Builds the command line to type into a terminal running `shellProgram` (PowerShell, cmd, bash, ...).
// Returns an empty string and fills *error when no suitable interpreter is installed.
QString commandFor(const QString& path, const QString& shellProgram, QString* error);

}
