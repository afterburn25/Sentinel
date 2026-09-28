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

$AppPath = (Resolve-Path $AppPath).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path

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

    # 1.0.15 sidebar: kHeader=78, first row starts at 94, row height=44.
    # Simulation index 5, Persona index 6, Model Lab index 7, Trainer index 8.
    Click-SaraClient -Window $main -X 100 -Y 328
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "03-simulation.png")

    Click-SaraClient -Window $main -X 100 -Y 372
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "04-persona-policy.png")

    Click-SaraClient -Window $main -X 100 -Y 416
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "05-model-lab.png")

    # Once Model Lab is active, its dedicated sidebar begins at y=138.
    # Train is the second Model Lab row, centered near y=186.
    Click-SaraClient -Window $main -X 100 -Y 186
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "06-trainer.png")

    # Datasets is the third Model Lab row, centered near y=242.
    Click-SaraClient -Window $main -X 100 -Y 242
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "07-datasets.png")

    # Personas & LoRAs is the fourth Model Lab row, centered near y=286.
    Click-SaraClient -Window $main -X 100 -Y 286
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "08-personas-loras.png")

    @"
SARA 1.0.15 RECOVERY UI CAPTURE
Executable: $AppPath
PID: $($proc.Id)
Captured: $([DateTime]::UtcNow.ToString("o"))
The screenshots came from the compiled packaged SARA.exe.
The main-window capture is also a startup-hang regression test.
"@ | Set-Content -Encoding utf8 (Join-Path $OutputDir "README.txt")
}
finally {
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}
