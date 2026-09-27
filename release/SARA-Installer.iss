#define MyAppName "SARA"
#define MyAppFullName "SARA - Synthetic Adaptive Response Agent"
#ifndef MyAppVersion
  #define MyAppVersion "1.0.18"
#endif
#ifndef SourceDir
  #define SourceDir "..\\package\\SARA-1.0.18-windows-x64"
#endif

[Setup]
AppId={{E68C9C6D-8BD4-4ECA-AE86-72B0B8B3B6A1}
AppName={#MyAppFullName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher=SARA Project
DefaultDirName={autopf}\\SARA
DefaultGroupName=SARA
DisableProgramGroupPage=yes
OutputDir=..\\installer-output
OutputBaseFilename=SARA-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
UninstallDisplayName={#MyAppName} {#MyAppVersion}
VersionInfoVersion={#MyAppVersion}.0
VersionInfoProductName={#MyAppFullName}
VersionInfoDescription={#MyAppFullName} Installer
VersionInfoCompany=SARA Project

[Files]
Source: "{#SourceDir}\\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\\SARA"; Filename: "{app}\\SARA.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\\SARA"; Filename: "{app}\\SARA.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Run]
Filename: "{app}\\SARA.exe"; Description: "Launch SARA"; Flags: nowait postinstall skipifsilent

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
end;
