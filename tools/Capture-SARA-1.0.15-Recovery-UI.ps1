param(
    [Parameter(Mandatory=$true)][string]$AppPath,
    [Parameter(Mandatory=$true)][string]$OutputDir
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class SaraRecoveryUiNative {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    public delegate bool EnumChildProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc proc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder className, int maxCount);

    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int maxCount);

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr hWnd, EnumChildProc proc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool IsHungAppWindow(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError=true)]
    public static extern IntPtr SendMessageTimeout(
        IntPtr hWnd,
        uint Msg,
        IntPtr wParam,
        IntPtr lParam,
        uint fuFlags,
        uint uTimeout,
        out IntPtr lpdwResult);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int cmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint flags);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);

    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern bool SetWindowText(IntPtr hWnd, string text);
}
"@


function Get-SaraStartupFailure {
    param([int]$ProcessId)

    $script:failureWindow = [IntPtr]::Zero
    $callback = [SaraRecoveryUiNative+EnumWindowsProc]{
        param([IntPtr]$hwnd, [IntPtr]$lParam)

        [uint32]$windowPid = 0
        [void][SaraRecoveryUiNative]::GetWindowThreadProcessId($hwnd, [ref]$windowPid)
        if ($windowPid -ne $ProcessId) { return $true }

        $title = New-Object Text.StringBuilder 512
        [void][SaraRecoveryUiNative]::GetWindowText($hwnd, $title, $title.Capacity)
        if ($title.ToString() -eq "Sentinel Startup Failed") {
            $script:failureWindow = $hwnd
            return $false
        }
        return $true
    }

    [void][SaraRecoveryUiNative]::EnumWindows($callback, [IntPtr]::Zero)
    if ($script:failureWindow -eq [IntPtr]::Zero) { return $null }

    $parts = New-Object System.Collections.Generic.List[string]
    $childCallback = [SaraRecoveryUiNative+EnumChildProc]{
        param([IntPtr]$child, [IntPtr]$lParam)
        $text = New-Object Text.StringBuilder 2048
        [void][SaraRecoveryUiNative]::GetWindowText($child, $text, $text.Capacity)
        $value = $text.ToString().Trim()
        if ($value) { $script:startupFailureParts.Add($value) }
        return $true
    }
    $script:startupFailureParts = $parts
    [void][SaraRecoveryUiNative]::EnumChildWindows($script:failureWindow, $childCallback, [IntPtr]::Zero)
    return ($parts -join " | ")
}

function Find-SaraWindow {
    param(
        [int]$ProcessId,
        [string]$ClassName,
        [int]$TimeoutSeconds,
        [switch]$RequireVisible
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($ClassName -eq "SARANativeWindow") {
            $startupFailure = Get-SaraStartupFailure -ProcessId $ProcessId
            if ($startupFailure) {
                throw "SARA startup failed before the main window opened: $startupFailure"
            }
        }

        if (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue | Where-Object { $_.HasExited }) {
            throw "SARA process exited before window class '$ClassName' became ready."
        }

        $script:matchedWindow = [IntPtr]::Zero
        $callback = [SaraRecoveryUiNative+EnumWindowsProc]{
            param([IntPtr]$hwnd, [IntPtr]$lParam)

            [uint32]$windowPid = 0
            [void][SaraRecoveryUiNative]::GetWindowThreadProcessId($hwnd, [ref]$windowPid)
            if ($windowPid -ne $ProcessId) { return $true }

            $sb = New-Object Text.StringBuilder 256
            [void][SaraRecoveryUiNative]::GetClassName($hwnd, $sb, $sb.Capacity)
            if ($sb.ToString() -eq $ClassName) {
                if ($RequireVisible -and -not [SaraRecoveryUiNative]::IsWindowVisible($hwnd)) {
                    return $true
                }
                $script:matchedWindow = $hwnd
                return $false
            }
            return $true
        }

        [void][SaraRecoveryUiNative]::EnumWindows($callback, [IntPtr]::Zero)
        if ($script:matchedWindow -ne [IntPtr]::Zero) {
            return $script:matchedWindow
        }

        Start-Sleep -Milliseconds 100
    }

    throw "Timed out waiting for SARA window class '$ClassName' for PID $ProcessId. This normally means startup is stuck before the main window becomes ready."
}

function Capture-SaraWindow {
    param(
        [IntPtr]$Window,
        [string]$Path
    )

    [SaraRecoveryUiNative+RECT]$rect = New-Object SaraRecoveryUiNative+RECT
    if (-not [SaraRecoveryUiNative]::GetWindowRect($Window, [ref]$rect)) {
        throw "GetWindowRect failed for $Path"
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -lt 200 -or $height -lt 150) {
        throw ("Unexpected SARA capture dimensions " + $width + "x" + $height + " for " + $Path)
    }

    if (-not [SaraRecoveryUiNative]::IsWindowVisible($Window)) {
        throw "Refusing to capture hidden SARA window: $Path"
    }
    [void][SaraRecoveryUiNative]::SetForegroundWindow($Window)
    Start-Sleep -Milliseconds 250

    $bitmap = New-Object System.Drawing.Bitmap(
        $width, $height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    )
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try {
        $printed = [SaraRecoveryUiNative]::PrintWindow($Window, $hdc, 2)
    }
    finally {
        $graphics.ReleaseHdc($hdc)
        $graphics.Dispose()
    }

    if (-not $printed) {
        throw "PrintWindow failed for $Path"
    }

    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)

    $colors = New-Object 'System.Collections.Generic.HashSet[int]'
    for ($gx=1; $gx -le 10; $gx++) {
        for ($gy=1; $gy -le 8; $gy++) {
            $px = [Math]::Min($width-1, [int]($width*$gx/11))
            $py = [Math]::Min($height-1, [int]($height*$gy/9))
            [void]$colors.Add($bitmap.GetPixel($px,$py).ToArgb())
        }
    }
    $bitmap.Dispose()

    if ($colors.Count -lt 4) {
        throw "Screenshot appears blank or unrendered: $Path"
    }

    Write-Host ("Captured " + $Path + " (" + $width + "x" + $height + ", sampled colors=" + $colors.Count + ")")
}

function Click-SaraClient {
    param(
        [IntPtr]$Window,
        [int]$X,
        [int]$Y
    )

    $packed = (($Y -band 0xffff) -shl 16) -bor ($X -band 0xffff)
    $lp = [IntPtr]$packed
    [void][SaraRecoveryUiNative]::PostMessage($Window, 0x0200, [IntPtr]::Zero, $lp)
    [void][SaraRecoveryUiNative]::PostMessage($Window, 0x0201, [IntPtr]1, $lp)
    Start-Sleep -Milliseconds 40
    [void][SaraRecoveryUiNative]::PostMessage($Window, 0x0202, [IntPtr]::Zero, $lp)
    Start-Sleep -Milliseconds 450
}

function Set-SaraControlText {
    param(
        [IntPtr]$Window,
        [int]$ControlId,
        [string]$Text
    )

    $control = [SaraRecoveryUiNative]::GetDlgItem($Window, $ControlId)
    if ($control -eq [IntPtr]::Zero) {
        throw "SARA control ID $ControlId was not found."
    }
    if (-not [SaraRecoveryUiNative]::SetWindowText($control, $Text)) {
        throw "Unable to set SARA control ID $ControlId."
    }
    Start-Sleep -Milliseconds 120
}

$AppPath = (Resolve-Path $AppPath).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path

$previousSaraDataRoot = $env:SARA_DATA_ROOT
$previousSaraCaptureFixture = $env:SARA_CAPTURE_FIXTURE
$captureDataRoot = Join-Path ([IO.Path]::GetTempPath()) ("sara-recovery-ui-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $captureDataRoot | Out-Null
$env:SARA_DATA_ROOT = $captureDataRoot
$env:SARA_CAPTURE_FIXTURE = "identity-research-v1"

$proc = Start-Process -FilePath $AppPath -WorkingDirectory (Split-Path $AppPath) -PassThru
try {
    $splash = Find-SaraWindow -ProcessId $proc.Id -ClassName "SARAStartupSplash" -TimeoutSeconds 5 -RequireVisible
    Capture-SaraWindow -Window $splash -Path (Join-Path $OutputDir "01-splash.png")

    # 1.0.15 deliberately keeps the splash up for at least seven seconds.
    # The recovery gate only passes if SARA naturally reveals its main window.
    $main = Find-SaraWindow -ProcessId $proc.Id -ClassName "SARANativeWindow" -TimeoutSeconds 30 -RequireVisible

    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $deadline -and [SaraRecoveryUiNative]::IsWindowVisible($splash)) {
        Start-Sleep -Milliseconds 100
    }
    if ([SaraRecoveryUiNative]::IsWindowVisible($splash)) {
        throw "SARA main window became visible but startup splash did not close."
    }
    # The original 1.0.15 startup intentionally blocks the UI thread while the
    # splash satisfies its seven-second minimum. Give the just-revealed window a
    # brief grace period to enter the normal message loop, then require WM_NULL
    # responsiveness instead of trusting the instantaneous ghost-window flag.
    $responsive = $false
    $responsiveDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $responsiveDeadline) {
        [IntPtr]$messageResult = [IntPtr]::Zero
        $sendResult = [SaraRecoveryUiNative]::SendMessageTimeout(
            $main, 0x0000, [IntPtr]::Zero, [IntPtr]::Zero,
            0x0002, 1000, [ref]$messageResult)
        if ($sendResult -ne [IntPtr]::Zero -and -not [SaraRecoveryUiNative]::IsHungAppWindow($main)) {
            $responsive = $true
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $responsive) {
        throw "SARA main window became visible but did not become responsive after startup."
    }

    Start-Sleep -Milliseconds 500
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "02-dashboard.png")

    # Permanent SARA product navigation.
    # kHeader=78; first main row starts at y=94; row height=44.
    $mainNavY = @(
        109, # Dashboard
        153, # Cases
        197, # Subjects & Identity
        241, # Simulation Chat
        285, # Personas
        329, # Channels & Messaging
        373, # Supervisor & Approvals
        417, # Evidence
        461, # Audit & Compliance
        505, # Model Lab
        549, # Agency Server
        593  # Settings
    )

    Click-SaraClient -Window $main -X 100 -Y $mainNavY[1]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "03-cases.png")

    # Runtime seeded a disposable case/subject because SARA_CAPTURE_FIXTURE
    # is set on the disposable data root. Navigate to Subjects and prove the
    # internal Research page really opened before accepting its screenshot.
    Click-SaraClient -Window $main -X 100 -Y $mainNavY[2]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "04-subjects-identity.png")

    [SaraRecoveryUiNative+RECT]$subjectClient = New-Object SaraRecoveryUiNative+RECT
    if (-not [SaraRecoveryUiNative]::GetClientRect($main, [ref]$subjectClient)) {
        throw "GetClientRect failed while calculating Subjects coordinates"
    }
    $subjectClientWidth = $subjectClient.Right - $subjectClient.Left
    $subjectX = 248.0
    $subjectContentW = $subjectClientWidth - $subjectX - 28.0
    $subjectLeftW = [Math]::Min(286.0,[Math]::Max(248.0,$subjectContentW * 0.34))
    $subjectRightX = $subjectX + $subjectLeftW + 14.0
    $subjectRightW = $subjectContentW - $subjectLeftW - 14.0
    $subjectBodyY = 78.0 + 94.0 + 78.0 + 12.0
    $researchX = [int]($subjectRightX + $subjectRightW - 370.0 + 42.0)
    Click-SaraClient -Window $main -X $researchX -Y ([int]($subjectBodyY + 26.0))

    $researchType = [SaraRecoveryUiNative]::GetDlgItem($main, 1071)
    if ($researchType -eq [IntPtr]::Zero -or -not [SaraRecoveryUiNative]::IsWindowVisible($researchType)) {
        throw "Identity Research did not open in the packaged SARA UI."
    }
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "04b-identity-research.png")

    # Open the contained provider registry and require its native provider-ID
    # editor to be visible before accepting the packaged screenshot.
    $providerManagerX = [int]($subjectClientWidth - 80)
    $providerManagerY = [int]($subjectBodyY + 25.0)
    Click-SaraClient -Window $main -X $providerManagerX -Y $providerManagerY

    $providerIdEdit = [SaraRecoveryUiNative]::GetDlgItem($main, 1079)
    if ($providerIdEdit -eq [IntPtr]::Zero -or -not [SaraRecoveryUiNative]::IsWindowVisible($providerIdEdit)) {
        throw "Identity Research Provider Registry did not open in the packaged SARA UI."
    }
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "04c-identity-research-providers.png")

    Click-SaraClient -Window $main -X 100 -Y $mainNavY[3]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "05-simulation-chat.png")

    Click-SaraClient -Window $main -X 100 -Y $mainNavY[4]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "06-personas.png")

    # Persona Rules & Learning is a protected internal Persona workflow.
    [SaraRecoveryUiNative+RECT]$personaClient = New-Object SaraRecoveryUiNative+RECT
    if (-not [SaraRecoveryUiNative]::GetClientRect($main, [ref]$personaClient)) {
        throw "GetClientRect failed while calculating Persona tab positions"
    }
    $personaClientWidth = $personaClient.Right - $personaClient.Left
    $personaTabBaseX = 248.0
    $personaTabGap = 8.0
    $personaContentWidth = $personaClientWidth - $personaTabBaseX - 28.0
    $personaTabWidth = ($personaContentWidth - ($personaTabGap * 5.0)) / 6.0
    $personaRulesX = [int]($personaTabBaseX + (5.0 * ($personaTabWidth + $personaTabGap)) + ($personaTabWidth / 2.0))
    Click-SaraClient -Window $main -X $personaRulesX -Y 202
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "06b-persona-rules-learning.png")

    # Open the first fixture rule. The Open control is rendered in the first
    # response-rule row. This verifies the packaged editor can enter in-place
    # edit state without creating a duplicate rule.
    $rulesContentX = 248.0
    $rulesContentW = $personaClientWidth - $rulesContentX - 28.0
    $ruleOpenX = [int]($rulesContentX + $rulesContentW - 214.0 + 25.0)
    Click-SaraClient -Window $main -X $ruleOpenX -Y 450
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "06c-persona-rule-edit.png")

    # Six fixture rules create two pages at four rows per page.
    $ruleNextX = [int]($rulesContentX + $rulesContentW - 76.0 + 17.0)
    Click-SaraClient -Window $main -X $ruleNextX -Y 399
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "06d-persona-rules-page-2.png")

    Click-SaraClient -Window $main -X 100 -Y $mainNavY[5]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "07-channels-messaging.png")

    Click-SaraClient -Window $main -X 100 -Y $mainNavY[6]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "08-supervisor-approvals.png")

    Click-SaraClient -Window $main -X 100 -Y $mainNavY[7]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "09-evidence.png")

    Click-SaraClient -Window $main -X 100 -Y $mainNavY[8]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "10-audit-compliance.png")

    Click-SaraClient -Window $main -X 100 -Y $mainNavY[9]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "11-model-lab-overview.png")

    # Model Lab is a contained workspace. Its internal tabs are across the top,
    # while the permanent SARA sidebar remains visible.
    [SaraRecoveryUiNative+RECT]$client = New-Object SaraRecoveryUiNative+RECT
    if (-not [SaraRecoveryUiNative]::GetClientRect($main, [ref]$client)) {
        throw "GetClientRect failed while calculating Model Lab tab positions"
    }
    $clientWidth = $client.Right - $client.Left
    $tabBaseX = 248.0
    $tabGap = 6.0
    $contentWidth = $clientWidth - $tabBaseX - 28.0
    $tabWidth = ($contentWidth - ($tabGap * 7.0)) / 8.0
    $tabY = 190

    function Click-ModelLabTab {
        param([int]$Index)
        $x = [int]($tabBaseX + ($Index * ($tabWidth + $tabGap)) + ($tabWidth / 2.0))
        Click-SaraClient -Window $main -X $x -Y $tabY
    }

    Click-ModelLabTab -Index 1
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "12-model-lab-train.png")

    Click-ModelLabTab -Index 2
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "13-model-lab-datasets.png")

    Click-ModelLabTab -Index 3
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "14-model-lab-personas-loras.png")

    Click-ModelLabTab -Index 4
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "15-model-lab-foundations.png")

    Click-ModelLabTab -Index 5
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "16-model-lab-jobs.png")

    Click-ModelLabTab -Index 6
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "17-model-lab-evaluation.png")

    Click-ModelLabTab -Index 7
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "18-model-lab-deployment.png")

    # Return to the permanent shell and prove the remaining operational pages.
    Click-SaraClient -Window $main -X 100 -Y $mainNavY[10]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "19-agency-server.png")

    Click-SaraClient -Window $main -X 100 -Y $mainNavY[11]
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "20-settings.png")

    @"
SARA 1.0.15 RECOVERY UI CAPTURE
Executable: $AppPath
PID: $($proc.Id)
Captured: $([DateTime]::UtcNow.ToString("o"))
The screenshots came from the compiled packaged SARA.exe.
The main-window capture is also a startup-hang regression test.
The capture sequence verifies the permanent investigative SARA shell:
Dashboard, Cases, Subjects & Identity, including the internal Identity Research workflow,
Simulation Chat, Personas, including the Rules & Learning Persona sub-workflow, Channels & Messaging,
Supervisor & Approvals, Evidence,
Audit & Compliance, Model Lab, Agency Server, and Settings.
Model Lab subpages are captured through internal top tabs while the SARA sidebar remains global.
"@ | Set-Content -Encoding utf8 (Join-Path $OutputDir "README.txt")
}
finally {
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
    if ($null -eq $previousSaraDataRoot) {
        Remove-Item Env:SARA_DATA_ROOT -ErrorAction SilentlyContinue
    } else {
        $env:SARA_DATA_ROOT = $previousSaraDataRoot
    }
    if ($null -eq $previousSaraCaptureFixture) {
        Remove-Item Env:SARA_CAPTURE_FIXTURE -ErrorAction SilentlyContinue
    } else {
        $env:SARA_CAPTURE_FIXTURE = $previousSaraCaptureFixture
    }
    Remove-Item -LiteralPath $captureDataRoot -Recurse -Force -ErrorAction SilentlyContinue
}
