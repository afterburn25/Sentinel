#define MyAppName "Sentinel"
#define MyAppVersion "1.0.8"
#define MyAppPublisher "Sentinel Project"
#define MyAppExeName "Sentinel.exe"
#define PackageDir "..\package\Sentinel-1.0.8-windows-x64"

[Setup]
AppId={{A6717D99-89F5-4C14-B4BE-2B42EACBC108}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\Sentinel
DefaultGroupName=Sentinel
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=output
OutputBaseFilename=Sentinel-Setup-1.0.8
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
SetupLogging=yes
UninstallDisplayIcon={app}\Sentinel.exe
VersionInfoVersion=1.0.8.0
VersionInfoCompany=Sentinel Project
VersionInfoDescription=Sentinel Installer
VersionInfoProductName=Sentinel
VersionInfoProductVersion=1.0.8.0

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
Name: "{app}\ai\models"
Name: "{app}\ai\runtime"
Name: "{app}\ai\runtime_cpu"
Name: "{app}\ai\logs"

[Icons]
Name: "{autodesktop}\Sentinel"; Filename: "{app}\Sentinel.exe"; WorkingDir: "{app}"
Name: "{userprograms}\Sentinel\Sentinel"; Filename: "{app}\Sentinel.exe"; WorkingDir: "{app}"
Name: "{userprograms}\Sentinel\Repair Local AI"; Filename: "{app}\Setup-Sentinel-AI.cmd"; WorkingDir: "{app}"

[Run]
Filename: "{app}\Sentinel.exe"; Description: "Launch Sentinel"; Flags: nowait postinstall skipifsilent

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
  ModelAlreadyValid: Boolean;
  RuntimeAlreadyValid: Boolean;
  GPUName: String;

procedure SetStatus(const S: String);
begin
  WizardForm.StatusLabel.Caption := S;
  WizardForm.StatusLabel.Update;
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

procedure StopSentinelAI;
var
  ResultCode: Integer;
begin
  SetStatus('Stopping the existing Sentinel AI service...');
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
  if CompareText(Item, ModelFileName) = 0 then
    SetStatus('Downloading Sentinel local model (~5.68 GB)...')
  else
    SetStatus('Downloading AI runtime: ' + Item);
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
    SetStatus('Existing Sentinel AI runtime is current; skipping runtime download.');
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
    SetStatus('Existing Sentinel model verified; skipping 5.68 GB model download.');
    ProgressPage.SetProgress(90, 100);
    exit;
  end;

  EnsureDir(ExpandConstant('{app}\ai\models'));
  SetStatus('Downloading Sentinel local model (~5.68 GB)...');
  DownloadOne(ModelURL, ModelFileName, ModelSHA256);
  TempModel := ExpandConstant('{tmp}\') + ModelFileName;

  if not FileSHA256Matches(TempModel, ModelSHA256) then
    RaiseException('Downloaded Sentinel model failed SHA-256 verification.');

  if FileExists(ModelPath) then
    DeleteFile(ModelPath);

  SetStatus('Installing verified Sentinel model...');
  if not RenameFile(TempModel, ModelPath) then
  begin
    if not FileCopy(TempModel, ModelPath, False) then
      RaiseException('Unable to move the verified Sentinel model into the application folder.');
    DeleteFile(TempModel);
  end;
  ProgressPage.SetProgress(90, 100);
end;

procedure ConfigureAI;
var
  ResultCode: Integer;
begin
  SetStatus('Configuring Sentinel to use the local AI model...');
  if not Exec(
      ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
      '-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "' +
      ExpandConstant('{app}\ai\Configure-Sentinel.ps1') + '"',
      ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) or
      (ResultCode <> 0) then
    RaiseException('Sentinel local model configuration failed.');

  ProgressPage.SetProgress(96, 100);
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(
    'Installing Sentinel local AI',
    'Sentinel Setup downloads only the components this computer still needs.',
    @OnDownloadProgress);
  ProgressPage := CreateOutputProgressPage(
    'Installing Sentinel',
    'Installing application files and preparing the local AI model.');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  StopSentinelAI;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    ProgressPage.Show;
    try
      ProgressPage.SetProgress(5, 100);
      SetStatus('Checking the existing Sentinel installation...');

      ModelAlreadyValid := FileSHA256Matches(ModelPath, ModelSHA256);
      RuntimeAlreadyValid := RuntimeValid;

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

      SetStatus('Finalizing Sentinel installation...');
      ProgressPage.SetProgress(100, 100);
      Sleep(300);
    finally
      ProgressPage.Hide;
    end;
  end;
end;
