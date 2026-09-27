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

public static class SaraUiNative {
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
        [int]$TimeoutSeconds = 20
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $callback = [SaraUiNative+EnumWindowsProc]{
            param([IntPtr]$hwnd, [IntPtr]$lParam)

            [uint32]$pid = 0
            [void][SaraUiNative]::GetWindowThreadProcessId($hwnd, [ref]$pid)
            if ($pid -ne $ProcessId) { return $true }

            $sb = New-Object Text.StringBuilder 256
            [void][SaraUiNative]::GetClassName($hwnd, $sb, $sb.Capacity)
            if ($sb.ToString() -eq $ClassName) {
                $script:matchedWindow = $hwnd
                return $false
            }
            return $true
        }

        $script:matchedWindow = [IntPtr]::Zero
        [void][SaraUiNative]::EnumWindows($callback, [IntPtr]::Zero)
        if ($script:matchedWindow -ne [IntPtr]::Zero) {
            return $script:matchedWindow
        }
        Start-Sleep -Milliseconds 100
    }

    throw "Timed out waiting for window class '$ClassName' for PID $ProcessId"
}

function Capture-SaraWindow {
    param(
        [IntPtr]$Window,
        [string]$Path
    )

    [SaraUiNative+RECT]$rect = New-Object SaraUiNative+RECT
    if (-not [SaraUiNative]::GetWindowRect($Window, [ref]$rect)) {
        throw "GetWindowRect failed for $Path"
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -lt 200 -or $height -lt 150) {
        throw ("Unexpected capture dimensions " + $width + "x" + $height + " for " + $Path)
    }

    [void][SaraUiNative]::ShowWindow($Window, 9)
    [void][SaraUiNative]::SetForegroundWindow($Window)
    Start-Sleep -Milliseconds 200

    $bitmap = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try {
        $printed = [SaraUiNative]::PrintWindow($Window, $hdc, 2)
    } finally {
        $graphics.ReleaseHdc($hdc)
        $graphics.Dispose()
    }

    if (-not $printed) {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $size = New-Object System.Drawing.Size($width,$height)
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $size)
        } finally {
            $graphics.Dispose()
        }
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
        throw ("Screenshot appears blank or unrendered: " + $Path + " (sample colors=" + $colors.Count + ")")
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
    [void][SaraUiNative]::PostMessage($Window, 0x0200, [IntPtr]::Zero, $lp)
    [void][SaraUiNative]::PostMessage($Window, 0x0201, [IntPtr]1, $lp)
    Start-Sleep -Milliseconds 40
    [void][SaraUiNative]::PostMessage($Window, 0x0202, [IntPtr]::Zero, $lp)
    Start-Sleep -Milliseconds 450
}

$AppPath = (Resolve-Path $AppPath).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path

$proc = Start-Process -FilePath $AppPath -WorkingDirectory (Split-Path $AppPath) -PassThru
try {
    $splash = Find-SaraWindow -ProcessId $proc.Id -ClassName "SARAStartupSplash" -TimeoutSeconds 5
    Capture-SaraWindow -Window $splash -Path (Join-Path $OutputDir "01-splash.png")

    $main = Find-SaraWindow -ProcessId $proc.Id -ClassName "SentinelNativeWindow" -TimeoutSeconds 20
    Start-Sleep -Milliseconds 700
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "02-main-dashboard.png")

    # Default sidebar: Model Lab is page index 7.
    Click-SaraClient -Window $main -X 100 -Y 450
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "03-model-lab-overview.png")

    # Model Lab rail: 0 Home, 1 Overview, 2 Train, 3 Datasets, 4 Personas, 5 Foundations, 6 Jobs, 7 Evaluation, 8 Deployment.
    Click-SaraClient -Window $main -X 100 -Y 212
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "04-model-lab-train.png")

    Click-SaraClient -Window $main -X 100 -Y 254
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "05-model-lab-datasets.png")

    Click-SaraClient -Window $main -X 100 -Y 296
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "06-model-lab-personas-loras.png")

    Click-SaraClient -Window $main -X 100 -Y 422
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "07-model-lab-evaluation.png")

    Click-SaraClient -Window $main -X 100 -Y 464
    Capture-SaraWindow -Window $main -Path (Join-Path $OutputDir "08-model-lab-deployment.png")

    @"
SARA ACTUAL BUILD UI SCREENSHOTS
Executable: $AppPath
PID: $($proc.Id)
Captured: $([DateTime]::UtcNow.ToString("o"))
Screenshots are captured from the compiled packaged SARA.exe, not generated mockups.
"@ | Set-Content -Encoding utf8 (Join-Path $OutputDir "README.txt")
}
finally {
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}
