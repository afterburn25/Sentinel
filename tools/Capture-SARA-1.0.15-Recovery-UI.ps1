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

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

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

function Find-SaraWindow {
    param(
        [int]$ProcessId,
        [string]$ClassName,
        [int]$TimeoutSeconds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $script:matchedWindow = [IntPtr]::Zero
        $callback = [SaraRecoveryUiNative+EnumWindowsProc]{
            param([IntPtr]$hwnd, [IntPtr]$lParam)

            [uint32]$windowPid = 0
            [void][SaraRecoveryUiNative]::GetWindowThreadProcessId($hwnd, [ref]$windowPid)
            if ($windowPid -ne $ProcessId) { return $true }

            $sb = New-Object Text.StringBuilder 256
            [void][SaraRecoveryUiNative]::GetClassName($hwnd, $sb, $sb.Capacity)
            if ($sb.ToString() -eq $ClassName) {
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

    [void][SaraRecoveryUiNative]::ShowWindow($Window, 9)
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
    $splash = Find-SaraWindow -ProcessId $proc.Id -ClassName "SARAStartupSplash" -TimeoutSeconds 5
    Capture-SaraWindow -Window $splash -Path (Join-Path $OutputDir "01-splash.png")

    # 1.0.15 deliberately keeps the splash up for at least seven seconds.
    # Requiring the real main class proves startup completed rather than hanging forever.
    $main = Find-SaraWindow -ProcessId $proc.Id -ClassName "SARANativeWindow" -TimeoutSeconds 25
    Start-Sleep -Milliseconds 800
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "02-dashboard.png")

    # 1.0.15 sidebar: kHeader=78, first row starts at 94, row height=44.
    # Simulation index 5, Persona index 6, Model Lab index 7, Trainer index 8.
    Click-SaraClient -Window $main -X 100 -Y 328
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "03-simulation.png")

    Click-SaraClient -Window $main -X 100 -Y 372
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "04-persona-policy.png")

    Click-SaraClient -Window $main -X 100 -Y 416
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "05-model-lab.png")

    Click-SaraClient -Window $main -X 100 -Y 460
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "06-trainer.png")

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
