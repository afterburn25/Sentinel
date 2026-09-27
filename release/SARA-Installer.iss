#define MyAppName "SARA"
#define MyAppFullName "SARA - Synthetic Adaptive Response Agent"
#ifndef MyAppVersion
  #define MyAppVersion "1.0.18"
#endif
#ifndef SourceDir
  #define SourceDir "..\\package\\SARA-1.0.18-windows-x64"
#endif

[Setup]
; Restore the stable SARA/Sentinel installer identity used by the approved 1.0.15 line.
AppId={{A6717D99-89F5-4C14-B4BE-2B42EACBC108}
AppName={#MyAppFullName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher=SARA Project
DefaultDirName={code:GetDefaultDirName}
DefaultGroupName=SARA
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\\installer-output
OutputBaseFilename=SARA-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
UsePreviousAppDir=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

; Approved SARA dark/neon installer visual baseline.
WizardStyle=modern dark polar hidebevels includetitlebar
WizardSizePercent=120,115
WizardBackColor=#020a12
WizardBackImageFile=..\\resources\\assets\\SARA-Splash.png
WizardBackImageOpacity=28
WizardImageFile=
WizardSmallImageFile=..\\resources\\assets\\SARA-Icon.png
WizardSmallImageBackColor=#020a12
SetupIconFile=..\\resources\\SARA.ico

UninstallDisplayName=SARA {#MyAppVersion}
UninstallDisplayIcon={app}\\SARA.exe
VersionInfoVersion={#MyAppVersion}.0
VersionInfoProductName={#MyAppFullName}
VersionInfoDescription={#MyAppFullName} Installer
VersionInfoCompany=SARA Project

[Files]
Source: "{#SourceDir}\\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: files; Name: "{autodesktop}\\Sentinel.lnk"
Type: filesandordirs; Name: "{userprograms}\\Sentinel"

[Icons]
Name: "{userprograms}\\SARA\\SARA"; Filename: "{app}\\SARA.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\\SARA"; Filename: "{app}\\SARA.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Run]
Filename: "{app}\\SARA.exe"; Description: "Launch SARA"; Flags: nowait postinstall skipifsilent

[Code]
var
  IsUpgrade: Boolean;
  PreviousInstallPath: String;
  PreviousInstallVersion: String;
  PreviousInstallName: String;
  UpgradeInfoPage: TOutputMsgWizardPage;
  FooterLabel: TNewStaticText;

function TryExistingUninstallRecord(RootKey: Integer; AppIdText: String): Boolean;
var
  Key, P, V, N: String;
begin
  Result := False;
  Key := 'Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\' + AppIdText + '_is1';
  if RegQueryStringValue(RootKey, Key, 'InstallLocation', P) then
  begin
    P := RemoveBackslashUnlessRoot(P);
    if (P <> '') and DirExists(P) then
    begin
      PreviousInstallPath := P;
      if not RegQueryStringValue(RootKey, Key, 'DisplayVersion', V) then V := '';
      if not RegQueryStringValue(RootKey, Key, 'DisplayName', N) then N := 'SARA';
      PreviousInstallVersion := V;
      PreviousInstallName := N;
      Result := True;
    end;
  end;
end;

function DetectPreviousInstallation: Boolean;
var
  SaraDir, LegacyDir: String;
begin
  PreviousInstallPath := '';
  PreviousInstallVersion := '';
  PreviousInstallName := '';

  ; Approved 1.0.15 AppId.
  if TryExistingUninstallRecord(HKCU, '{A6717D99-89F5-4C14-B4BE-2B42EACBC108}') then begin Result := True; exit; end;
  if TryExistingUninstallRecord(HKLM64, '{A6717D99-89F5-4C14-B4BE-2B42EACBC108}') then begin Result := True; exit; end;

  ; Also recognize the short-lived post-1.0.15 installer identity so users can recover cleanly.
  if TryExistingUninstallRecord(HKCU, '{E68C9C6D-8BD4-4ECA-AE86-72B0B8B3B6A1}') then begin Result := True; exit; end;
  if TryExistingUninstallRecord(HKLM64, '{E68C9C6D-8BD4-4ECA-AE86-72B0B8B3B6A1}') then begin Result := True; exit; end;

  SaraDir := ExpandConstant('{localappdata}\\Programs\\SARA');
  LegacyDir := ExpandConstant('{localappdata}\\Programs\\Sentinel');

  if DirExists(SaraDir) then
  begin
    PreviousInstallPath := SaraDir;
    PreviousInstallName := 'SARA';
    PreviousInstallVersion := 'Existing installation';
    Result := True;
    exit;
  end;

  if DirExists(LegacyDir) then
  begin
    PreviousInstallPath := LegacyDir;
    PreviousInstallName := 'Sentinel / SARA';
    PreviousInstallVersion := 'Legacy installation';
    Result := True;
    exit;
  end;

  Result := False;
end;

function GetDefaultDirName(Param: String): String;
begin
  if IsUpgrade and (PreviousInstallPath <> '') then
    Result := PreviousInstallPath
  else
    Result := ExpandConstant('{localappdata}\\Programs\\SARA');
end;

function InitializeSetup(): Boolean;
begin
  IsUpgrade := DetectPreviousInstallation;
  Result := True;
end;

procedure InitializeWizard;
var
  ExistingText: String;
begin
  WizardForm.Caption := 'SARA Setup';

  ; Keep the SARA identity visible across every page.
  FooterLabel := TNewStaticText.Create(WizardForm);
  FooterLabel.Parent := WizardForm;
  FooterLabel.Caption := 'SAME DATA   •   MORE CAPABILITIES   •   A BETTER SARA';
  FooterLabel.Font.Style := [fsBold];
  FooterLabel.Font.Color := $00FFD718;
  FooterLabel.Left := ScaleX(18);
  FooterLabel.Top := WizardForm.ClientHeight - ScaleY(49);
  FooterLabel.Width := WizardForm.ClientWidth - ScaleX(210);
  FooterLabel.Height := ScaleY(20);

  if IsUpgrade then
  begin
    WizardForm.Caption := 'SARA Setup — Upgrade Existing Installation';
    WizardForm.WelcomeLabel1.Caption := 'Upgrade Existing Installation';
    WizardForm.WelcomeLabel2.Caption :=
      'SARA found a previous installation and will upgrade it in place.' + #13#10 + #13#10 +
      'Your data, personas, models, settings, training records, and logs are preserved.';

    ExistingText :=
      'Previous Installation Detected' + #13#10 + #13#10 +
      'Product: ' + PreviousInstallName + #13#10 +
      'Installed version: ' + PreviousInstallVersion + #13#10 +
      'Installation path: ' + PreviousInstallPath + #13#10 +
      'Data path: ' + ExpandConstant('{localappdata}\\SARA') + #13#10 + #13#10 +
      'Setup will replace application files while preserving existing SARA/Sentinel data.';

    UpgradeInfoPage := CreateOutputMsgPage(
      wpWelcome,
      'Previous Installation Detected',
      'SARA is already installed on this system.',
      ExistingText);

    WizardForm.NextButton.Caption := '&Upgrade';
  end
  else
  begin
    WizardForm.WelcomeLabel1.Caption := 'Install SARA';
    WizardForm.WelcomeLabel2.Caption :=
      'Synthetic Adaptive Response Agent' + #13#10 + #13#10 +
      'Install the native SARA application while keeping all local AI and project data under your control.';
    WizardForm.NextButton.Caption := '&Install';
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if IsUpgrade then
  begin
    WizardForm.Caption := 'SARA Setup — Upgrade Existing Installation';
    if CurPageID = wpSelectDir then
    begin
      WizardForm.PageNameLabel.Caption := 'Confirm Upgrade Location';
      WizardForm.PageDescriptionLabel.Caption :=
        'The existing SARA application will be upgraded in place.';
    end
    else if CurPageID = wpReady then
    begin
      WizardForm.PageNameLabel.Caption := 'Ready to Upgrade';
      WizardForm.PageDescriptionLabel.Caption :=
        'Existing data and local state will be preserved.';
      WizardForm.ReadyLabel.Caption :=
        'Click Upgrade to replace the application files while keeping your data, personas, models, settings, training records, and logs.';
      WizardForm.NextButton.Caption := '&Upgrade';
    end
    else if CurPageID = wpInstalling then
    begin
      WizardForm.PageNameLabel.Caption := 'Upgrading SARA';
      WizardForm.PageDescriptionLabel.Caption := 'Installing the new SARA application files.';
      WizardForm.StatusLabel.Caption := 'Upgrading SARA...';
    end
    else if CurPageID = wpFinished then
    begin
      WizardForm.PageNameLabel.Caption := 'SARA Upgrade Complete';
      WizardForm.PageDescriptionLabel.Caption := 'Your existing SARA installation has been upgraded.';
    end;
  end
  else
  begin
    WizardForm.Caption := 'SARA Setup';
    if CurPageID = wpReady then
    begin
      WizardForm.PageNameLabel.Caption := 'Ready to Install';
      WizardForm.PageDescriptionLabel.Caption := 'Setup is ready to install SARA.';
      WizardForm.NextButton.Caption := '&Install';
    end;
  end;
end;
