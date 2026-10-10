<#
.SYNOPSIS
  CrucibleCode - one-shot setup for Windows: installs dependencies, builds the IDE and starts it.

.DESCRIPTION
  Run it from a clone of the repository (or pass -Repo to clone a fork first):

      powershell -ExecutionPolicy Bypass -File scripts\setup.ps1
      scripts\setup.cmd                       (double-click friendly wrapper)

  Administrator rights are only needed to install the Microsoft C++ Build Tools and Python.
  If something has to be installed the script asks Windows for elevation (UAC) by itself, installs
  it in an elevated helper and then continues building as your normal user. Nothing is changed when
  everything is already present.

.PARAMETER Config     Release (default) or Debug
.PARAMETER NoRun      Only install and build, do not start the IDE
.PARAMETER Clean      Delete the build folder first
.PARAMETER Repo       Git URL of the repository to clone (your fork) when run outside a checkout
.PARAMETER QtVersion  Qt version to download with aqtinstall (default 6.7.3)
.PARAMETER WithDotNet Also install the .NET SDK (needed to build/run C# projects inside the IDE)
.PARAMETER Phase      Internal: 'Install' is the elevated helper phase
#>
param(
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    [switch]$NoRun,
    [switch]$Clean,
    [string]$Repo = $env:CRUCIBLE_REPO,
    [string]$QtVersion = $(if ($env:CRUCIBLE_QT_VERSION) { $env:CRUCIBLE_QT_VERSION } else { '6.7.3' }),
    [switch]$WithDotNet,
    [ValidateSet('All', 'Install')][string]$Phase = 'All',
    [string]$RootDir = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

function Write-Step($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok($m)   { Write-Host " ok $m" -ForegroundColor Green }
function Write-Warn2($m) { Write-Host " !! $m" -ForegroundColor Yellow }
function Fail($m)       { Write-Host " xx $m" -ForegroundColor Red; throw $m }

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user    = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

function Find-Command($name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($c) { return $c.Source }
    return $null
}

# ---- locate the sources ------------------------------------------------------------------
function Find-Root {
    if ($RootDir -and (Test-Path (Join-Path $RootDir 'CMakeLists.txt'))) { return (Resolve-Path $RootDir).Path }
    $candidates = @()
    if ($PSScriptRoot) { $candidates += (Join-Path $PSScriptRoot '..') }
    $candidates += (Get-Location).Path
    foreach ($c in $candidates) {
        $cm = Join-Path $c 'CMakeLists.txt'
        if ((Test-Path $cm) -and (Select-String -Path $cm -Pattern 'CrucibleCode' -Quiet)) { return (Resolve-Path $c).Path }
    }
    return $null
}

# ---- tool detection -----------------------------------------------------------------------
function Find-Python {
    foreach ($cand in @(@('py', '-3'), @('python'), @('python3'))) {
        $exe = Find-Command $cand[0]
        if (-not $exe) { continue }
        $extra = @()
        if ($cand.Count -gt 1) { $extra = @($cand[1..($cand.Count - 1)]) }
        try {
            $out = & $exe @extra -c 'import sys; print(sys.version_info[0])' 2>$null
            if ($LASTEXITCODE -eq 0 -and "$out".Trim() -eq '3') {
                $result = @($exe) + $extra
                return ,$result     # the comma keeps it an array even with a single element
            }
        } catch {}
    }
    return $null
}

function Find-VsInstall {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $p = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($p) { return ("$p".Trim() -split "`r?`n")[0] }
    return $null
}

function Find-QtPrefix {
    if ($env:CRUCIBLE_QT -and (Test-Path (Join-Path $env:CRUCIBLE_QT 'bin\qmake.exe'))) { return $env:CRUCIBLE_QT }
    $roots = @("$env:USERPROFILE\Qt", 'C:\Qt')
    $best = $null; $bestVer = [version]'0.0'
    foreach ($r in $roots) {
        if (-not (Test-Path $r)) { continue }
        foreach ($v in Get-ChildItem $r -Directory -ErrorAction SilentlyContinue) {
            $ver = $null
            if (-not [version]::TryParse($v.Name, [ref]$ver)) { continue }
            if ($ver -lt [version]'6.4') { continue }
            foreach ($k in Get-ChildItem $v.FullName -Directory -Filter 'msvc*_64' -ErrorAction SilentlyContinue) {
                if ((Test-Path (Join-Path $k.FullName 'bin\qmake.exe')) -and $ver -gt $bestVer) { $best = $k.FullName; $bestVer = $ver }
            }
        }
    }
    return $best
}

function Install-WithWinget($id, $extra) {
    $winget = Find-Command 'winget'
    if (-not $winget) { Fail "winget is not available. Install '$id' manually (or install 'App Installer' from the Microsoft Store) and re-run." }
    $wargs = @('install', '-e', '--id', $id, '--accept-package-agreements', '--accept-source-agreements', '--silent')
    if ($extra) { $wargs += $extra }
    Write-Step "winget install $id"
    & $winget @wargs
    # -1978335189 (0x8A150011) = already installed / no applicable upgrade
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { Write-Warn2 "winget returned $LASTEXITCODE for $id" }
}

function Get-SelfPath {
    if ($PSCommandPath) { return $PSCommandPath }
    Fail 'Elevation needs the script on disk: download it, then run  powershell -ExecutionPolicy Bypass -File setup.ps1'
}

# ---- elevated phase: system-wide installs ---------------------------------------------------
function Invoke-InstallPhase {
    Write-Step 'Installing system components (administrator)'
    if (-not (Find-Python))    { Install-WithWinget 'Python.Python.3.12' $null }
    if (-not (Find-VsInstall)) {
        Write-Warn2 'Installing the Microsoft C++ Build Tools (several GB, this takes a while)...'
        Install-WithWinget 'Microsoft.VisualStudio.2022.BuildTools' @('--override',
            '--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
    }
    if (-not (Find-Command 'git') -and $Repo) { Install-WithWinget 'Git.Git' $null }
    if ($WithDotNet -and -not (Find-Command 'dotnet')) { Install-WithWinget 'Microsoft.DotNet.SDK.8' $null }
    Write-Ok 'System components installed'
}

if ($Phase -eq 'Install') {
    try { Invoke-InstallPhase }
    catch { Write-Host $_ -ForegroundColor Red; Read-Host 'Installation failed. Press Enter to close' }
    return
}

# ---- main -------------------------------------------------------------------------------------
Write-Host ''
Write-Host '  CrucibleCode setup' -ForegroundColor Cyan
Write-Host ''

# 1. sources
$Root = Find-Root
if (-not $Root) {
    if (-not $Repo) { Fail 'Sources not found. Run from a clone of the repository or pass -Repo <git url>.' }
    if (-not (Find-Command 'git')) {
        if (Test-Admin) { Install-WithWinget 'Git.Git' $null; Update-SessionPath }
        else {
            Write-Step 'git is needed to clone the repository: asking for administrator rights'
            $self = Get-SelfPath
            $hostExe = (Get-Process -Id $PID).Path
            Start-Process $hostExe -Verb RunAs -Wait -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$self`"", '-Phase', 'Install', '-Repo', $Repo)
            Update-SessionPath
        }
    }
    $Root = Join-Path (Get-Location).Path 'cruciblecode'
    Write-Step "Cloning $Repo"
    & git clone --depth 1 $Repo $Root
    if ($LASTEXITCODE -ne 0) { Fail 'git clone failed' }
}
Set-Location $Root
Write-Ok "Sources: $Root"

# 2. what is missing?
$needSystem = $false
$qt = Find-QtPrefix
$vs = Find-VsInstall
if (-not $vs) { $needSystem = $true; Write-Warn2 'Microsoft C++ Build Tools (MSVC) not found' }
if (-not $qt -and -not (Find-Python)) { $needSystem = $true; Write-Warn2 'Python 3 not found (needed to download Qt)' }
if ($WithDotNet -and -not (Find-Command 'dotnet')) { $needSystem = $true }

if ($needSystem) {
    if (Test-Admin) {
        Invoke-InstallPhase
    } else {
        Write-Step 'Administrator rights are needed to install missing components. Windows will ask for permission (UAC)...'
        $self = Get-SelfPath
        $hostExe = (Get-Process -Id $PID).Path
        $helperArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$self`"", '-Phase', 'Install')
        if ($WithDotNet) { $helperArgs += '-WithDotNet' }
        try { Start-Process $hostExe -Verb RunAs -Wait -ArgumentList $helperArgs }
        catch { Fail 'Elevation was cancelled. Re-run and accept the prompt, or start this script from an Administrator PowerShell.' }
    }
    Update-SessionPath
    $vs = Find-VsInstall
    if (-not $vs) { Fail 'The Microsoft C++ Build Tools are still missing (a reboot may be required after installation). Re-run the script afterwards.' }
}
Write-Ok "MSVC: $vs"

# 3. per-user tools: cmake, ninja, aqtinstall in a private virtual environment (no admin needed)
$toolsDir = Join-Path $env:LOCALAPPDATA 'CrucibleCode'
$venv = Join-Path $toolsDir 'venv'
$venvScripts = Join-Path $venv 'Scripts'
$haveCmake = [bool](Find-Command 'cmake'); $haveNinja = [bool](Find-Command 'ninja')
if (-not $qt -or -not $haveCmake -or -not $haveNinja) {
    $py = Find-Python
    if (-not $py) { Fail 'Python 3 is required but was not found. Install it from https://www.python.org and re-run.' }
    if (-not (Test-Path (Join-Path $venvScripts 'python.exe'))) {
        Write-Step 'Creating a private Python environment'
        New-Item -ItemType Directory -Force $toolsDir | Out-Null
        $pyArgs = @(); if ($py.Count -gt 1) { $pyArgs += $py[1..($py.Count - 1)] }
        & $py[0] @pyArgs -m venv $venv
        if ($LASTEXITCODE -ne 0) { Fail 'Could not create the Python virtual environment' }
    }
    $pkgs = @('aqtinstall')
    if (-not $haveCmake) { $pkgs += 'cmake' }
    if (-not $haveNinja) { $pkgs += 'ninja' }
    Write-Step ("pip install " + ($pkgs -join ' '))
    & (Join-Path $venvScripts 'python.exe') -m pip install --quiet --upgrade pip
    & (Join-Path $venvScripts 'python.exe') -m pip install --quiet --upgrade @pkgs
    if ($LASTEXITCODE -ne 0) { Fail 'pip install failed' }
    $env:Path = "$venvScripts;$env:Path"
}

# 4. Qt
if (-not $qt) {
    $qtRoot = Join-Path $env:USERPROFILE 'Qt'
    Write-Step "Downloading Qt $QtVersion (MSVC 2019 64-bit build, works with Visual Studio 2022) - about 500 MB"
    & (Join-Path $venvScripts 'aqt.exe') install-qt windows desktop $QtVersion win64_msvc2019_64 --outputdir $qtRoot
    if ($LASTEXITCODE -ne 0) { Fail 'aqtinstall failed to download Qt' }
    $qt = Join-Path $qtRoot "$QtVersion\msvc2019_64"
    if (-not (Test-Path (Join-Path $qt 'bin\qmake.exe'))) { Fail "Qt was downloaded but $qt looks incomplete" }
}
Write-Ok "Qt: $qt"

# 5. Visual Studio build environment (vcvars64) imported into this session
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { Fail "vcvars64.bat not found in $vs" }
Write-Step 'Loading the MSVC build environment'
$envBat = Join-Path $env:TEMP 'crucible-msvc-env.bat'
Set-Content -Path $envBat -Encoding ASCII -Value @('@echo off', "call `"$vcvars`" >nul 2>&1", 'set')
$envDump = & cmd.exe /c $envBat
foreach ($line in $envDump) {
    $i = $line.IndexOf('=')
    if ($i -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $i), $line.Substring($i + 1), 'Process') }
}
if (Test-Path $venvScripts) { $env:Path = "$venvScripts;$env:Path" }

# 6. configure + build
$build = Join-Path $Root 'build'
if ($Clean -and (Test-Path $build)) { Write-Step 'Cleaning build folder'; Remove-Item $build -Recurse -Force }
Write-Step "Configuring ($Config)"
$qtCmake = $qt -replace '\\', '/'
& cmake -S $Root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$qtCmake"
if ($LASTEXITCODE -ne 0) { Fail 'CMake configure failed' }
Write-Step 'Building'
& cmake --build $build --parallel
if ($LASTEXITCODE -ne 0) { Fail 'Build failed' }

# 7. make the exe runnable: copy Qt DLLs and plugins next to it
$exe = Join-Path $build 'CrucibleCode.exe'
if (-not (Test-Path $exe)) { Fail "Build finished but $exe was not produced" }
$deploy = Join-Path $qt 'bin\windeployqt.exe'
if (-not (Test-Path $deploy)) { $deploy = Join-Path $qt 'bin\windeployqt6.exe' }
if (Test-Path $deploy) {
    Write-Step 'Deploying Qt runtime next to the executable'
    $mode = if ($Config -eq 'Debug') { '--debug' } else { '--release' }
    & $deploy $mode --no-translations $exe | Out-Null
}
Write-Ok "Built $exe"

# 8. run
if (-not $NoRun) {
    Write-Step 'Starting CrucibleCode'
    Start-Process -FilePath $exe -ArgumentList @("`"$Root`"")
} else {
    Write-Ok "Run it with:  $exe"
}
