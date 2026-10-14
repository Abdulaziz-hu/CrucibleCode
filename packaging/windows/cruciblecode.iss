; Inno Setup script for CrucibleCode. Built by build-windows.ps1, which passes:
;   /DAppVersion=0.7.0  /DSourceDir=<staged app folder>  /DOutputDir=<dist>  /DOutputName=<file name without .exe>
;   /DRepoDir=<repository root>
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef OutputName
  #define OutputName "CrucibleCode-setup"
#endif

[Setup]
AppId={{6C0A3E55-4D7B-4A52-9E3B-5F1D2A7C9B10}
AppName=CrucibleCode
AppVersion={#AppVersion}
AppVerName=CrucibleCode {#AppVersion}
AppPublisher=CrucibleCode contributors
AppPublisherURL=https://github.com/Abdulaziz-hu/CrucibleCode
AppSupportURL=https://github.com/Abdulaziz-hu/CrucibleCode/issues
AppUpdatesURL=https://github.com/Abdulaziz-hu/CrucibleCode/releases
DefaultDirName={autopf}\CrucibleCode
DefaultGroupName=CrucibleCode
DisableProgramGroupPage=yes
LicenseFile={#RepoDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
SetupIconFile={#RepoDir}\resources\icons\icon.ico
UninstallDisplayIcon={app}\CrucibleCode.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; per-user by default (no admin prompt); the wizard offers "install for all users"
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\CrucibleCode"; Filename: "{app}\CrucibleCode.exe"
Name: "{autodesktop}\CrucibleCode"; Filename: "{app}\CrucibleCode.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\CrucibleCode.exe"; Description: "{cm:LaunchProgram,CrucibleCode}"; Flags: nowait postinstall skipifsilent
