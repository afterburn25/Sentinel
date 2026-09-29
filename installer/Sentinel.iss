#define MyAppName "SARA"
#ifndef MyAppVersion
  #error MyAppVersion must be supplied from the repository VERSION file.
#endif
#define MyAppPublisher "SARA Project"
#define MyAppExeName "SARA.exe"
#define PackageDir "..\package\SARA-windows-x64"

[Setup]
AppId={{A6717D99-89F5-4C14-B4BE-2B42EACBC108}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={code:GetDefaultDirName}
DefaultGroupName=SARA
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=output
OutputBaseFilename=SARA-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\resources\SARA.ico
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
SetupLogging=yes
UninstallDisplayIcon={app}\SARA.exe
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany=SARA Project
VersionInfoDescription=SARA Installer
VersionInfoProductName=SARA
VersionInfoProductVersion={#MyAppVersion}.0

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: files; Name: "{autodesktop}\Sentinel.lnk"
Type: filesandordirs; Name: "{userprograms}\Sentinel"

[Dirs]
Name: "{app}\ai\models"
Name: "{app}\ai\runtime"
Name: "{app}\ai\runtime_cpu"
Name: "{app}\ai\logs"

[Icons]
Name: "{autodesktop}\SARA"; Filename: "{app}\SARA.exe"; WorkingDir: "{app}"
Name: "{userprograms}\SARA\SARA"; Filename: "{app}\SARA.exe"; WorkingDir: "{app}"
Name: "{userprograms}\SARA\Repair Local AI"; Filename: "{app}\Setup-Sentinel-AI.cmd"; WorkingDir: "{app}"

[Run]
Filename: "{app}\SARA.exe"; Description: "Launch SARA"; Flags: nowait postinstall skipifsilent

[Code]
const
  LlamaBuild = 'b10977';
  ModelFileName = 'Qwen3.5-9B-Q4_K_M.gguf';
  ModelSHA256 = '03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8';
  ModelURL = 'https://huggingface.co/llmware/qwen3.5-9b-gguf/resolve/main/Qwen3.5-9B-Q4_K_M.gguf?download=true';
  LlamaBaseURL = 'https://github.com/ggml-org/llama.cpp/releases/download/b10977/';

var
  DownloadPage: TDownloadWizardPage;
  ProgressPage: TOutputProgressWizardPage;
  UpgradeInfoPage: TOutputMsgWizardPage;
  ModelAlreadyValid: Boolean;
  RuntimeAlreadyValid: Boolean;
  IsUpgrade: Boolean;
  PreviousInstallPath: String;
  PreviousInstallVersion: String;
  PreviousInstallName: String;
  GPUName: String;
  LastDownloadItem: String;

function GetDefaultDirName(Param: String): String;
var
  LegacyDir, SaraDir: String;
begin
  LegacyDir := ExpandConstant('{localappdata}\Programs\Sentinel');
  SaraDir := ExpandConstant('{localappdata}\Programs\SARA');

  if IsUpgrade and (PreviousInstallPath <> '') then
    Result := PreviousInstallPath
  else if DirExists(SaraDir) then
    Result := SaraDir
  else if DirExists(LegacyDir) then
    Result := LegacyDir
  else
    Result := SaraDir;
end;

function DetectPreviousInstallation: Boolean;
var
  SaraDir, LegacyDir, RegPath, RegVersion: String;
begin
  PreviousInstallPath := '';
  PreviousInstallVersion := '';
  PreviousInstallName := '';

  { First check the stable AppId uninstall record. This is the authoritative
    previous-install check for normal SARA/Sentinel installs. }
  if RegQueryStringValue(
       HKCU,
       'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A6717D99-89F5-4C14-B4BE-2B42EACBC108}_is1',
       'InstallLocation',
       RegPath) then
  begin
    RegPath := RemoveBackslashUnlessRoot(RegPath);
    if (RegPath <> '') and DirExists(RegPath) then
    begin
      PreviousInstallPath := RegPath;
      PreviousInstallName := 'SARA';
      if RegQueryStringValue(
           HKCU,
           'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A6717D99-89F5-4C14-B4BE-2B42EACBC108}_is1',
           'DisplayVersion',
           RegVersion) then
        PreviousInstallVersion := RegVersion;
    end;
  end;

  { Also check HKLM in case an older build was installed with elevation. }
  if (PreviousInstallPath = '') and RegQueryStringValue(
       HKLM,
       'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A6717D99-89F5-4C14-B4BE-2B42EACBC108}_is1',
       'InstallLocation',
       RegPath) then
  begin
    RegPath := RemoveBackslashUnlessRoot(RegPath);
    if (RegPath <> '') and DirExists(RegPath) then
    begin
      PreviousInstallPath := RegPath;
      PreviousInstallName := 'SARA';
      if RegQueryStringValue(
           HKLM,
           'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A6717D99-89F5-4C14-B4BE-2B42EACBC108}_is1',
           'DisplayVersion',
           RegVersion) then
        PreviousInstallVersion := RegVersion;
    end;
  end;

  { Fall back to known folders so pre-AppId Sentinel builds are still detected. }
  SaraDir := ExpandConstant('{localappdata}\Programs\SARA');
  LegacyDir := ExpandConstant('{localappdata}\Programs\Sentinel');

  if PreviousInstallPath = '' then
  begin
    if FileExists(AddBackslash(SaraDir) + 'SARA.exe') then
    begin
      PreviousInstallPath := SaraDir;
      PreviousInstallName := 'SARA';
    end
    else if FileExists(AddBackslash(LegacyDir) + 'SARA.exe') or
            FileExists(AddBackslash(LegacyDir) + 'Sentinel.exe') then
    begin
      PreviousInstallPath := LegacyDir;
      PreviousInstallName := 'Sentinel';
    end;
  end;

  Result := PreviousInstallPath <> '';
end;

function InitializeSetup: Boolean;
begin
  IsUpgrade := DetectPreviousInstallation;
  if IsUpgrade then
    Log('Previous SARA/Sentinel installation detected at: ' + PreviousInstallPath)
  else
    Log('No previous SARA/Sentinel installation detected. Fresh install mode.');
  Result := True;
end;


procedure SetStatus(const S: String);
begin
  WizardForm.StatusLabel.Caption := S;
  if ProgressPage <> nil then
    ProgressPage.SetText(S, '');
  Log(S);
end;

function FileSHA256Matches(const FileName, Expected: String): Boolean;
var
  Actual: String;
begin
  Result := False;
  if not FileExists(FileName) then
    exit;

  try
    Actual := Lowercase(GetSHA256OfFile(FileName));
    Result := CompareText(Actual, Lowercase(Expected)) = 0;
  except
    Log('Unable to calculate SHA-256 for ' + FileName);
  end;
end;


procedure StopSentinelApp;
var
  ResultCode: Integer;
begin
  SetStatus('Closing SARA before upgrade...');

  { Ask Windows to close any running Sentinel instance first. }
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM SARA.exe',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM Sentinel.exe',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  { Give SARA and legacy Sentinel instances a moment to finish their shutdown path. }
  Sleep(1200);

  { Force-close only if either process is still holding upgrade files. }
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM SARA.exe',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM Sentinel.exe',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure StopSentinelAI;
var
  ResultCode: Integer;
begin
  SetStatus('Stopping the existing SARA AI service...');
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM llama-server.exe',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function DetectGPUName: String;
var
  ResultCode: Integer;
  OutFile, CommandLine: String;
  Lines: TArrayOfString;
begin
  Result := '';
  OutFile := ExpandConstant('{tmp}\sentinel-gpu.txt');
  DeleteFile(OutFile);

  CommandLine :=
    '-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command ' +
    '"$g=(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | ' +
    'Where-Object {$_.Name -match ''NVIDIA''} | Select-Object -First 1 -ExpandProperty Name); ' +
    'if($g){Set-Content -LiteralPath ''' + OutFile + ''' -Value $g -Encoding ASCII}"';

  if Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
      CommandLine, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    if LoadStringsFromFile(OutFile, Lines) and (GetArrayLength(Lines) > 0) then
      Result := Trim(Lines[0]);
  end;
end;

procedure EnsureDir(const Dir: String);
begin
  if not DirExists(Dir) then
    ForceDirectories(Dir);
end;

procedure RemoveDirTree(const Dir: String);
begin
  if DirExists(Dir) then
    DelTree(Dir, True, True, True);
  ForceDirectories(Dir);
end;

procedure ExtractZip(const ZipFile, DestDir: String);
var
  ResultCode: Integer;
  PS, Params: String;
begin
  EnsureDir(DestDir);
  PS := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  Params :=
    '-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command ' +
    '"Expand-Archive -LiteralPath ''' + ZipFile + ''' -DestinationPath ''' + DestDir + ''' -Force"';

  if not Exec(PS, Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
    RaiseException('Unable to extract ' + ExtractFileName(ZipFile));
end;

function RuntimeMarkerPath: String;
begin
  Result := ExpandConstant('{app}\ai\runtime.version');
end;

function ModelPath: String;
begin
  Result := ExpandConstant('{app}\ai\models\') + ModelFileName;
end;

function ModelMarkerPath: String;
begin
  Result := ExpandConstant('{app}\ai\models\') + ModelFileName + '.sha256';
end;

function ModelMarkerValid: Boolean;
var
  Lines: TArrayOfString;
begin
  Result := False;
  if not FileExists(ModelPath) then
    exit;
  if not FileExists(ModelMarkerPath) then
    exit;
  if not LoadStringsFromFile(ModelMarkerPath, Lines) then
    exit;
  if GetArrayLength(Lines) = 0 then
    exit;
  Result := CompareText(Trim(Lines[0]), ModelSHA256) = 0;
end;

procedure SaveModelMarker;
begin
  SaveStringToFile(ModelMarkerPath, ModelSHA256 + #13#10, False);
end;

function VerifyInstalledModel: Boolean;
begin
  if ModelMarkerValid then
  begin
    SetStatus('Existing SARA model verification marker is current; skipping full 5.68 GB checksum scan.');
    Result := True;
    exit;
  end;

  if not FileExists(ModelPath) then
  begin
    Result := False;
    exit;
  end;

  ProgressPage.SetProgress(8, 100);
  ProgressPage.SetProgress(7, 100);
  SetStatus(
    'Verifying the existing 5.68 GB SARA model. This can take a minute on slower drives; Setup is still working...');
  Result := FileSHA256Matches(ModelPath, ModelSHA256);
  if Result then
    SaveModelMarker;
end;

procedure CheckPersonaLoraState;
var
  SaraData, LegacyData, RuntimeConfig: String;
begin
  ProgressPage.SetProgress(11, 100);
  SetStatus('Checking saved persona LoRA configuration...');
  SaraData := ExpandConstant('{localappdata}\SARA');
  LegacyData := ExpandConstant('{localappdata}\Sentinel');

  if DirExists(SaraData) then
    RuntimeConfig := AddBackslash(SaraData) + 'active-runtime.ini'
  else
    RuntimeConfig := AddBackslash(LegacyData) + 'active-runtime.ini';

  if FileExists(RuntimeConfig) then
    Log('Existing persona foundation/LoRA runtime configuration will be preserved: ' + RuntimeConfig)
  else
    Log('No existing persona LoRA runtime configuration found; this is normal until a persona adapter is bound.');

  ProgressPage.SetProgress(12, 100);
  SetStatus('Persona LoRA configuration check complete.');
end;

function RuntimeValid: Boolean;
var
  Lines: TArrayOfString;
begin
  Result := False;
  if not FileExists(RuntimeMarkerPath) then
    exit;
  if not LoadStringsFromFile(RuntimeMarkerPath, Lines) then
    exit;
  if (GetArrayLength(Lines) = 0) or (Trim(Lines[0]) <> LlamaBuild) then
    exit;

  Result :=
    FileExists(ExpandConstant('{app}\ai\runtime\llama-server.exe')) or
    FileExists(ExpandConstant('{app}\ai\runtime_cpu\llama-server.exe'));

  if not Result then
  begin
    Result :=
      (FileSearch('llama-server.exe', ExpandConstant('{app}\ai\runtime')) <> '') or
      (FileSearch('llama-server.exe', ExpandConstant('{app}\ai\runtime_cpu')) <> '');
  end;
end;

function OnDownloadProgress(
  const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
var
  Item: String;
begin
  Item := ExtractFileName(FileName);
  if CompareText(Item, LastDownloadItem) <> 0 then
  begin
    LastDownloadItem := Item;
    if CompareText(Item, ModelFileName) = 0 then
      Log('Downloading SARA local model (~5.68 GB)...')
    else
      Log('Downloading AI runtime: ' + Item);
  end;
  Result := True;
end;

procedure DownloadOne(const URL, FileName, SHA256: String);
begin
  DownloadPage.Clear;
  DownloadPage.Add(URL, FileName, SHA256);
  DownloadPage.Show;
  try
    DownloadPage.Download;
  finally
    DownloadPage.Hide;
  end;
end;

procedure InstallRuntime;
var
  MainAsset, CudaAsset, CPUAsset: String;
  MainZip, CudaZip, CPUZip: String;
begin
  if RuntimeAlreadyValid then
  begin
    SetStatus('Existing SARA AI runtime is current; skipping runtime download.');
    ProgressPage.SetProgress(45, 100);
    exit;
  end;

  StopSentinelAI;
  GPUName := DetectGPUName;

  if Pos('NVIDIA', Uppercase(GPUName)) > 0 then
  begin
    if (Pos('RTX 50', Uppercase(GPUName)) > 0) or
       (Pos('RTX 5090', Uppercase(GPUName)) > 0) or
       (Pos('RTX 5080', Uppercase(GPUName)) > 0) or
       (Pos('RTX 5070', Uppercase(GPUName)) > 0) or
       (Pos('RTX 5060', Uppercase(GPUName)) > 0) then
    begin
      MainAsset := 'llama-' + LlamaBuild + '-bin-win-cuda-13.4-x64.zip';
      CudaAsset := 'cudart-llama-bin-win-cuda-13.4-x64.zip';
    end
    else
    begin
      MainAsset := 'llama-' + LlamaBuild + '-bin-win-cuda-12.4-x64.zip';
      CudaAsset := 'cudart-llama-bin-win-cuda-12.4-x64.zip';
    end;
  end
  else
  begin
    MainAsset := 'llama-' + LlamaBuild + '-bin-win-cpu-x64.zip';
    CudaAsset := '';
  end;

  CPUAsset := 'llama-' + LlamaBuild + '-bin-win-cpu-x64.zip';

  SetStatus('Downloading primary llama.cpp runtime...');
  DownloadOne(LlamaBaseURL + MainAsset, MainAsset, '');
  MainZip := ExpandConstant('{tmp}\') + MainAsset;

  RemoveDirTree(ExpandConstant('{app}\ai\runtime'));
  SetStatus('Installing primary llama.cpp runtime...');
  ExtractZip(MainZip, ExpandConstant('{app}\ai\runtime'));
  ProgressPage.SetProgress(25, 100);

  if CudaAsset <> '' then
  begin
    SetStatus('Downloading NVIDIA CUDA runtime support...');
    DownloadOne(LlamaBaseURL + CudaAsset, CudaAsset, '');
    CudaZip := ExpandConstant('{tmp}\') + CudaAsset;
    SetStatus('Installing NVIDIA CUDA runtime support...');
    ExtractZip(CudaZip, ExpandConstant('{app}\ai\runtime'));
  end;
  ProgressPage.SetProgress(35, 100);

  SetStatus('Downloading CPU fallback runtime...');
  DownloadOne(LlamaBaseURL + CPUAsset, CPUAsset, '');
  CPUZip := ExpandConstant('{tmp}\') + CPUAsset;

  RemoveDirTree(ExpandConstant('{app}\ai\runtime_cpu'));
  SetStatus('Installing CPU fallback runtime...');
  ExtractZip(CPUZip, ExpandConstant('{app}\ai\runtime_cpu'));
  SaveStringToFile(RuntimeMarkerPath, LlamaBuild + #13#10, False);
  ProgressPage.SetProgress(45, 100);
end;

procedure InstallModel;
var
  TempModel: String;
begin
  if ModelAlreadyValid then
  begin
    SetStatus('Existing SARA model verified; skipping 5.68 GB model download.');
    ProgressPage.SetProgress(90, 100);
    exit;
  end;

  EnsureDir(ExpandConstant('{app}\ai\models'));
  SetStatus('Downloading SARA local model (~5.68 GB)...');
  DownloadOne(ModelURL, ModelFileName, ModelSHA256);
  TempModel := ExpandConstant('{tmp}\') + ModelFileName;

  if not FileSHA256Matches(TempModel, ModelSHA256) then
    RaiseException('Downloaded SARA model failed SHA-256 verification.');

  if FileExists(ModelPath) then
    DeleteFile(ModelPath);

  SetStatus('Installing verified SARA model...');
  if not RenameFile(TempModel, ModelPath) then
  begin
    if not FileCopy(TempModel, ModelPath, False) then
      RaiseException('Unable to move the verified SARA model into the application folder.');
    DeleteFile(TempModel);
  end;
  SaveModelMarker;
  ProgressPage.SetProgress(90, 100);
end;

procedure ConfigureAI;
var
  ResultCode: Integer;
begin
  SetStatus('Configuring SARA to use the local AI model...');
  if not Exec(
      ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
      '-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "' +
      ExpandConstant('{app}\ai\Configure-Sentinel.ps1') + '"',
      ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) or
      (ResultCode <> 0) then
    RaiseException('SARA local model configuration failed.');

  ProgressPage.SetProgress(96, 100);
end;

procedure InitializeWizard;
var
  ProgressTitle, ProgressText: String;
begin
  if IsUpgrade then
  begin
    WizardForm.Caption := 'SARA Upgrade';
    WizardForm.WelcomeLabel1.Caption := 'Welcome to the SARA Upgrade Wizard';
    WizardForm.WelcomeLabel2.Caption :=
      'Setup found a previous installation of SARA/Sentinel.' + #13#10 + #13#10 +
      'This wizard will upgrade the existing installation in place. Your existing ' +
      'application data will be preserved, and the local AI model will not be downloaded ' +
      'again when the installed copy verifies correctly.';

    UpgradeInfoPage := CreateOutputMsgPage(
      wpWelcome,
      'Previous Installation Detected',
      'SARA will upgrade the existing installation.',
      'Existing product: ' + PreviousInstallName + #13#10 +
      'Installed version: ' + PreviousInstallVersion + #13#10 +
      'Existing installation:' + #13#10 +
      PreviousInstallPath + #13#10 + #13#10 +
      'Setup detected this installation before starting the normal setup flow. ' +
      'It will close SARA automatically, replace the application files, preserve ' +
      'existing SARA/Sentinel data, and reuse the installed local AI model when valid.');

    ProgressTitle := 'Upgrading SARA';
    ProgressText := 'Replacing application files and checking the local AI components.';
  end
  else
  begin
    ProgressTitle := 'Installing SARA';
    ProgressText := 'Installing application files and preparing the local AI model.';
  end;

  DownloadPage := CreateDownloadPage(
    'Preparing SARA local AI',
    'SARA Setup downloads only the components this computer still needs.',
    @OnDownloadProgress);
  DownloadPage.ShowBaseNameInsteadOfUrl := True;

  ProgressPage := CreateOutputProgressPage(
    ProgressTitle,
    ProgressText);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if IsUpgrade then
  begin
    WizardForm.Caption := 'SARA Upgrade';

    if CurPageID = wpWelcome then
    begin
      WizardForm.PageNameLabel.Caption := 'SARA Upgrade';
      WizardForm.PageDescriptionLabel.Caption :=
        'A previous SARA/Sentinel installation was detected.';
    end
    else if CurPageID = wpSelectDir then
    begin
      WizardForm.PageNameLabel.Caption := 'Confirm Upgrade Location';
      WizardForm.PageDescriptionLabel.Caption :=
        'SARA will be upgraded in the existing installation folder.';
    end
    else if CurPageID = wpReady then
    begin
      WizardForm.PageNameLabel.Caption := 'Ready to Upgrade';
      WizardForm.PageDescriptionLabel.Caption :=
        'Setup is ready to upgrade your existing SARA installation.';
      WizardForm.ReadyLabel.Caption :=
        'Click Upgrade to update SARA while preserving your existing data and valid local AI model.';
      WizardForm.NextButton.Caption := '&Upgrade';
    end
    else if CurPageID = wpPreparing then
    begin
      WizardForm.PageNameLabel.Caption := 'Preparing SARA Upgrade';
      WizardForm.PageDescriptionLabel.Caption :=
        'Setup is preparing to upgrade your existing SARA installation.';
      WizardForm.StatusLabel.Caption := 'Preparing upgrade...';
    end
    else if CurPageID = wpInstalling then
    begin
      WizardForm.PageNameLabel.Caption := 'Upgrading SARA';
      WizardForm.PageDescriptionLabel.Caption :=
        'Please wait while Setup upgrades SARA on your computer.';
      WizardForm.StatusLabel.Caption := 'Upgrading SARA...';
    end
    else if CurPageID = wpFinished then
    begin
      WizardForm.PageNameLabel.Caption := 'SARA Upgrade Complete';
      WizardForm.PageDescriptionLabel.Caption :=
        'The existing SARA installation has been upgraded.';
    end;
  end
  else if CurPageID = wpReady then
  begin
    WizardForm.NextButton.Caption := '&Install';
    WizardForm.ReadyLabel.Caption :=
      'Setup is ready to install SARA on this computer.';
  end;
end;

procedure CurInstallProgressChanged(CurProgress, MaxProgress: Integer);
begin
  if IsUpgrade then
    WizardForm.StatusLabel.Caption := 'Upgrading SARA...';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if IsUpgrade then
    StopSentinelApp;
  StopSentinelAI;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    ProgressPage.Show;
    try
      ProgressPage.SetProgress(5, 100);
      SetStatus('Checking the existing SARA installation...');

      SetStatus('Checking the existing SARA AI runtime...');
      RuntimeAlreadyValid := RuntimeValid;

      ModelAlreadyValid := VerifyInstalledModel;
      CheckPersonaLoraState;

      if ModelAlreadyValid then
        Log('Existing model checksum is valid; model download will be skipped.')
      else
        Log('Model is missing or invalid; installer will download a verified copy.');

      if RuntimeAlreadyValid then
        Log('Existing llama.cpp runtime matches ' + LlamaBuild + '.')
      else
        Log('llama.cpp runtime is missing or outdated.');

      ProgressPage.SetProgress(10, 100);
      InstallRuntime;
      InstallModel;
      ConfigureAI;

      if IsUpgrade then
        SetStatus('Finalizing SARA upgrade...')
      else
        SetStatus('Finalizing SARA installation...');
      ProgressPage.SetProgress(100, 100);
      Sleep(300);
    finally
      ProgressPage.Hide;
    end;
  end;
end;
