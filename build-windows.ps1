<#
.SYNOPSIS
  CrucibleCode - build + package for Windows.
.EXAMPLE
  ./build-windows.ps1
  ./build-windows.ps1 -Formats portable,msi
  ./build-windows.ps1 -QtDir C:\Qt\6.7.3\msvc2022_64 -Clean
#>
[CmdletBinding()]
param(
    [string[]]$Formats = @(),
    [string]$Version = "",
    [string]$QtDir = "",
    [string]$BuildDir = "build\release-windows",
    [string]$Generator = "",
    [switch]$NoKSyntax,
    [switch]$SkipBuild,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Log($m)  { Write-Host "==> $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host " ok $m" -ForegroundColor Green }
function Warn($m) { Write-Host " !! $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host " xx $m" -ForegroundColor Red; exit 1 }
function Have($n) { return [bool](Get-Command $n -ErrorAction SilentlyContinue) }
# Run a native command, discard its output, and never let stderr/exit codes throw. Returns the exit code.
function Invoke-Quiet([scriptblock]$Block) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Block 2>&1 | Out-Null; return $LASTEXITCODE }
    catch { return 1 }
    finally { $ErrorActionPreference = $prev }
}

if ($env:OS -ne 'Windows_NT') { Die "build-windows.ps1 must be run on Windows. On Linux use ./build-linux.sh." }

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

# ---- formats -----------------------------------------------------------------------------------
$cleanedFormats = @()
foreach ($entry in $Formats) {
    foreach ($part in $entry.Split(',')) {
        $trimmed = $part.Trim().ToLower()
        if ($trimmed) { $cleanedFormats += $trimmed }
    }
}
$Formats = $cleanedFormats
$explicit = $Formats.Count -gt 0

function Want($fmt) { return (-not $explicit) -or ($Formats -contains $fmt) }

foreach ($f in $Formats) {
    if (@('portable', 'installer', 'msi') -notcontains $f) { Die "unknown format '$f' (portable, installer, msi)" }
}

# ---- version -----------------------------------------------------------------------------------
if (-not $Version) {
    $cm = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw
    if ($cm -match '(?s)project\(.*?VERSION\s+([0-9][0-9.]*)') { $Version = $Matches[1] } else { Die "could not read the version from CMakeLists.txt (use -Version)" }
}

$VersionParts = @()
foreach ($seg in $Version.Split('.')) {
    $digits = $seg -replace '[^0-9]', ''
    if ($digits) { $VersionParts += [int]$digits } else { $VersionParts += 0 }
}
while ($VersionParts.Count -lt 3) { $VersionParts += 0 }
$MsiVersion = "$($VersionParts[0]).$($VersionParts[1]).$($VersionParts[2])"
Log "CrucibleCode $Version for Windows x64"

# ---- tools: cmake ------------------------------------------------------------------------------
if (-not (Have 'cmake')) {
    $cmakePaths = @("$env:ProgramFiles\CMake\bin", "${env:ProgramFiles(x86)}\CMake\bin")
    foreach ($p in $cmakePaths) {
        if (Test-Path (Join-Path $p 'cmake.exe')) { $env:PATH = "$p;$env:PATH"; break }
    }
}
if (-not (Have 'cmake')) { Die "cmake not found. Install it (winget install Kitware.CMake) or run scripts\setup.ps1." }

# ---- tools: Qt kit -----------------------------------------------------------------------------
function Find-QtKit {
    $cands = @()
    if ($QtDir) { $cands += $QtDir }
    if ($env:Qt6_DIR) { $cands += $env:Qt6_DIR }
    if ($env:QTDIR) { $cands += $env:QTDIR }
    if ($env:CMAKE_PREFIX_PATH) {
        foreach ($cp in ($env:CMAKE_PREFIX_PATH -split ';')) { if ($cp) { $cands += $cp } }
    }
    foreach ($c in $cands) {
        $d = $c
        for ($j = 0; $j -lt 4 -and $d; $j++) {
            if (Test-Path (Join-Path $d 'bin\windeployqt.exe')) { return (Resolve-Path $d).Path }
            $d = Split-Path -Parent $d
        }
    }
    $bases = @('C:\Qt', "$env:USERPROFILE\Qt")
    foreach ($base in $bases) {
        if (-not (Test-Path $base)) { continue }
        $kits = @(Get-ChildItem -Path $base -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^[0-9]+(\.[0-9]+)*$' } |
                Sort-Object { [version]$_.Name } -Descending)
        foreach ($k in $kits) {
            $sub = @(Get-ChildItem -Path $k.FullName -Directory -ErrorAction SilentlyContinue |
                   Where-Object { $_.Name -match '^(msvc|mingw)' -and (Test-Path (Join-Path $_.FullName 'bin\windeployqt.exe')) } |
                   Sort-Object Name -Descending)
            if ($sub.Count -gt 0) { return $sub[0].FullName }
        }
    }
    return $null
}
$Qt = Find-QtKit
if (-not $Qt) { Die "No Qt 6 kit found. Install Qt 6 (see scripts\setup.ps1) or pass -QtDir C:\Qt\6.7.3\msvc2022_64" }
Ok "Qt kit: $Qt"
$IsMinGw = (Split-Path -Leaf $Qt) -like 'mingw*'

# ---- tools: compiler environment ---------------------------------------------------------------
if ($IsMinGw) {
    $mg = Get-ChildItem 'C:\Qt\Tools' -Directory -Filter 'mingw*_64' -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
    if ($mg) { $env:PATH = "$($mg.FullName)\bin;$env:PATH" }
    if (-not (Have 'g++')) { Die "MinGW g++ not found (install the matching MinGW from the Qt maintenance tool)." }
} elseif (-not (Have 'cl')) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { Die "Visual Studio (C++ build tools) not found. Install 'Visual Studio Build Tools' with the C++ workload." }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { Die "Visual Studio is installed without the C++ x64 tools." }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    Log "loading the Visual Studio x64 environment"
    $envLines = cmd /c "`"$vcvars`" >nul && set"
    foreach ($line in $envLines) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
}

# ---- configure + build -------------------------------------------------------------------------
$BuildPath = Join-Path $Root $BuildDir
if ($Clean -and (Test-Path $BuildPath)) { Log "cleaning $BuildDir"; Remove-Item -Recurse -Force $BuildPath }
if (-not $SkipBuild) {
    $gen = $Generator
    if (-not $gen) { if (Have 'ninja') { $gen = 'Ninja' } elseif ($IsMinGw) { $gen = 'MinGW Makefiles' } else { $gen = 'NMake Makefiles' } }
    $ks = if ($NoKSyntax) { 'OFF' } else { 'ON' }
    Log "configuring (Release, generator: $gen, KSyntaxHighlighting=$ks)"

    & cmake -S $Root -B $BuildPath -G $gen "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_PREFIX_PATH=$Qt" "-DCRUCIBLE_USE_KSYNTAX=$ks"
    if ($LASTEXITCODE -ne 0) { Die "cmake configure failed" }
    Log "building"
    & cmake --build $BuildPath --config Release --parallel
    if ($LASTEXITCODE -ne 0) { Die "build failed" }
} elseif (-not (Test-Path $BuildPath)) { Die "-SkipBuild given but $BuildDir does not exist" }

$Exe = Get-ChildItem -Path $BuildPath -Recurse -Filter 'CrucibleCode.exe' -ErrorAction SilentlyContinue |
       Where-Object { $_.FullName -notmatch '\\(stage|CMakeFiles)\\' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Exe) { Die "CrucibleCode.exe was not found under $BuildDir" }

# ---- stage: the exe + Qt runtime ---------------------------------------------------------------
$Dist = Join-Path $Root 'dist'
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$StageRoot = Join-Path $BuildPath 'stage'
if (Test-Path $StageRoot) { Remove-Item -Recurse -Force $StageRoot }

function New-Stage([string]$InstallType) {
    $dir = Join-Path $StageRoot $InstallType
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Copy-Item $Exe.FullName (Join-Path $dir 'CrucibleCode.exe')
    $env:PATH = "$Qt\bin;$env:PATH"
    & (Join-Path $Qt 'bin\windeployqt.exe') --release --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime (Join-Path $dir 'CrucibleCode.exe') | Out-Null
    if ($LASTEXITCODE -ne 0) { Die "windeployqt failed" }
    Copy-Item (Join-Path $Root 'LICENSE') $dir
    if (Test-Path (Join-Path $Root 'THIRD_PARTY.md')) { Copy-Item (Join-Path $Root 'THIRD_PARTY.md') $dir }
    Set-Content -Path (Join-Path $dir 'install-type.txt') -Value $InstallType -Encoding ascii -NoNewline
    return $dir
}

function Write-Sum([string]$File) {
    $h = (Get-FileHash -Algorithm SHA256 -Path $File).Hash.ToLower()
    Set-Content -Path "$File.sha256" -Value "$h  $(Split-Path -Leaf $File)" -Encoding ascii
    Ok "dist\$(Split-Path -Leaf $File)"
}

$built = @()

# ---- portable ZIP ------------------------------------------------------------------------------
if (Want 'portable') {
    Log "creating the portable ZIP"
    $dir = New-Stage 'portable'
    $zipRoot = Join-Path $StageRoot 'zip'
    $inner = Join-Path $zipRoot 'CrucibleCode'
    New-Item -ItemType Directory -Force -Path $inner | Out-Null
    Copy-Item -Path (Join-Path $dir '*') -Destination $inner -Recurse
    $zip = Join-Path $Dist "CrucibleCode-$Version-windows-x64-portable.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path $inner -DestinationPath $zip -CompressionLevel Optimal
    Write-Sum $zip
    $built += $zip
}

# ---- installer (Inno Setup) --------------------------------------------------------------------
if (Want 'installer') {
    $iscc = $null
    $innoPaths = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")
    foreach ($p in $innoPaths) {
        if (Test-Path $p) { $iscc = $p; break }
    }
    if (-not $iscc -and (Have 'iscc')) { $iscc = (Get-Command iscc).Source }
    if (-not $iscc -and (Have 'winget')) {
        Log "Inno Setup not found: trying winget"
        [void](Invoke-Quiet { winget install -e --id JRSoftware.InnoSetup --accept-package-agreements --accept-source-agreements --silent })
        foreach ($p in $innoPaths) {
            if (Test-Path $p) { $iscc = $p; break }
        }
    }
    if (-not $iscc) {
        $msg = "Inno Setup 6 not found (https://jrsoftware.org/isdl.php): skipping the installer"
        if ($explicit) { Die $msg } else { Warn $msg }
    } else {
        Log "creating the installer with Inno Setup"
        $dir = New-Stage 'installer'
        $name = "CrucibleCode-$Version-windows-x64-setup"
        & $iscc "/Qp" "/DAppVersion=$Version" "/DSourceDir=$dir" "/DOutputDir=$Dist" "/DOutputName=$name" "/DRepoDir=$Root" (Join-Path $Root 'packaging\windows\cruciblecode.iss')
        if ($LASTEXITCODE -ne 0) { Die "Inno Setup failed" }
        Write-Sum (Join-Path $Dist "$name.exe")
        $built += (Join-Path $Dist "$name.exe")
    }
}

# ---- MSI (WiX v4+) -----------------------------------------------------------------------------
if (Want 'msi') {
    $wix = $null
    if (Have 'wix') { $wix = 'wix' }
    elseif (Have 'dotnet') {
        $hasSdk = $false
        $sdkOut = & { $ErrorActionPreference = 'Continue'; dotnet --list-sdks 2>$null }
        if ($sdkOut) { $hasSdk = $true }
        if ($hasSdk) {
            Log "WiX not found: installing the 'wix' dotnet tool"
            [void](Invoke-Quiet { dotnet tool install --global wix })
            $env:PATH = "$env:USERPROFILE\.dotnet\tools;$env:PATH"
            if (Have 'wix') { $wix = 'wix' }
        } else {
            Warn "dotnet is installed but has no SDK (runtime only); install it with: winget install Microsoft.DotNet.SDK.8"
        }
    }
    if (-not $wix) {
        $msg = "WiX Toolset v4+ not available (install the .NET SDK: winget install Microsoft.DotNet.SDK.8, then open a new terminal and re-run, or: dotnet tool install --global wix): skipping the MSI"
        if ($explicit) { Die $msg } else { Warn $msg }
    } else {
        Log "creating the MSI with WiX"
        [void](Invoke-Quiet { wix extension add -g WixToolset.UI.wixext })
        $dir = New-Stage 'msi'
        $work = Join-Path $StageRoot 'wix'
        New-Item -ItemType Directory -Force -Path $work | Out-Null

        $lic = (Get-Content (Join-Path $Root 'LICENSE') -Raw) -replace '\\', '\\' -replace '\{', '\{' -replace '\}', '\}'
        $lic = ($lic -replace "`r?`n", "\par`n")
        Set-Content -Path (Join-Path $work 'license.rtf') -Value ("{\rtf1\ansi\deff0{\fonttbl{\f0 Segoe UI;}}\f0\fs18 " + $lic + "}") -Encoding ascii

        $upgradeCode = '6C0A3E55-4D7B-4A52-9E3B-5F1D2A7C9B11'

        $wxs = @'
<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs" xmlns:ui="http://wixtoolset.org/schemas/v4/wxs/ui">
  <Package Name="CrucibleCode" Manufacturer="CrucibleCode contributors" Version="$MsiVersion"
           UpgradeCode="$upgradeCode" Scope="perMachine" Compressed="yes">
    <MajorUpgrade DowngradeErrorMessage="A newer version of CrucibleCode is already installed." />
    <MediaTemplate EmbedCab="yes" />
    <Icon Id="AppIcon" SourceFile="$Root\resources\icons\icon.ico" />
    <Property Id="ARPPRODUCTICON" Value="AppIcon" />
    <Property Id="ARPURLINFOABOUT" Value="https://github.com/Abdulaziz-hu/CrucibleCode" />
    <WixVariable Id="WixUILicenseRtf" Value="$work\license.rtf" />

    <StandardDirectory Id="ProgramFiles64Folder">
      <Directory Id="INSTALLFOLDER" Name="CrucibleCode">
        <Files Include="$dir\**" />
      </Directory>
    </StandardDirectory>
    <StandardDirectory Id="ProgramMenuFolder">
      <Directory Id="AppMenuDir" Name="CrucibleCode" />
    </StandardDirectory>

    <Component Id="StartMenuShortcut" Directory="AppMenuDir" Guid="*">
      <Shortcut Id="AppShortcut" Name="CrucibleCode" Description="Lightweight IDE for C, C++ and C#"
                Target="[INSTALLFOLDER]CrucibleCode.exe" WorkingDirectory="INSTALLFOLDER" />
      <RemoveFolder Id="RemoveAppMenuDir" On="uninstall" />
      <RegistryValue Root="HKCU" Key="Software\CrucibleCode" Name="StartMenuShortcut" Type="integer" Value="1" KeyPath="yes" />
    </Component>

    <ui:WixUI Id="WixUI_InstallDir" InstallDirectory="INSTALLFOLDER" />
  </Package>
</Wix>
'@

        $wxs = $ExecutionContext.InvokeCommand.ExpandString($wxs)

        $wxsPath = Join-Path $work 'CrucibleCode.wxs'
        Set-Content -Path $wxsPath -Value $wxs -Encoding utf8
        $msi = Join-Path $Dist "CrucibleCode-$Version-windows-x64.msi"
        if (Test-Path $msi) { Remove-Item $msi }
        & wix build -arch x64 -ext WixToolset.UI.wixext -o $msi $wxsPath
        if ($LASTEXITCODE -ne 0) { Die "wix build failed" }
        Write-Sum $msi
        $built += $msi
    }
}

Log "done. Packages are in $Dist"
foreach ($file in @(Get-ChildItem $Dist -File)) {
    if ($file.Extension -ne '.sha256') {
        "    {0,-62} {1,8:N1} MB" -f $file.Name, ($file.Length / 1MB)
    }
}
