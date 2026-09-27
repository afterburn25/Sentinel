param(
    [Parameter(Mandatory=$true)][string]$InstallerPath,
    [Parameter(Mandatory=$true)][string]$OutputPath
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class SaraInstallerNative {
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

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int maxCount);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int cmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint flags);
}
"@

function Find-InstallerWindow {
    param([int]$ProcessId,[int]$TimeoutSeconds=15)

    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while([DateTime]::UtcNow -lt $deadline) {
        $script:installerWindow=[IntPtr]::Zero
        $callback=[SaraInstallerNative+EnumWindowsProc]{
            param([IntPtr]$hwnd,[IntPtr]$lParam)
            [uint32]$windowPid=0
            [void][SaraInstallerNative]::GetWindowThreadProcessId($hwnd,[ref]$windowPid)
            if($windowPid -ne $ProcessId -or -not [SaraInstallerNative]::IsWindowVisible($hwnd)) { return $true }

            $sb=New-Object Text.StringBuilder 512
            [void][SaraInstallerNative]::GetWindowText($hwnd,$sb,$sb.Capacity)
            if($sb.ToString().Contains("SARA")) {
                $script:installerWindow=$hwnd
                return $false
            }
            return $true
        }
        [void][SaraInstallerNative]::EnumWindows($callback,[IntPtr]::Zero)
        if($script:installerWindow -ne [IntPtr]::Zero) { return $script:installerWindow }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for SARA installer wizard"
}

function Capture-Window {
    param([IntPtr]$Window,[string]$Path)

    [SaraInstallerNative+RECT]$rect=New-Object SaraInstallerNative+RECT
    if(-not [SaraInstallerNative]::GetWindowRect($Window,[ref]$rect)) { throw "GetWindowRect failed" }
    $width=$rect.Right-$rect.Left
    $height=$rect.Bottom-$rect.Top
    if($width -lt 400 -or $height -lt 300) {
        throw ("Unexpected installer window size: " + $width + "x" + $height)
    }

    [void][SaraInstallerNative]::ShowWindow($Window,9)
    [void][SaraInstallerNative]::SetForegroundWindow($Window)
    Start-Sleep -Milliseconds 250

    $bitmap=New-Object System.Drawing.Bitmap($width,$height,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics=[System.Drawing.Graphics]::FromImage($bitmap)
    $hdc=$graphics.GetHdc()
    try {
        $ok=[SaraInstallerNative]::PrintWindow($Window,$hdc,2)
    } finally {
        $graphics.ReleaseHdc($hdc)
        $graphics.Dispose()
    }
    if(-not $ok) { throw "PrintWindow failed for installer" }

    $bitmap.Save($Path,[System.Drawing.Imaging.ImageFormat]::Png)
    $colors=New-Object 'System.Collections.Generic.HashSet[int]'
    for($gx=1;$gx -le 8;$gx++) {
        for($gy=1;$gy -le 6;$gy++) {
            $px=[Math]::Min($width-1,[int]($width*$gx/9))
            $py=[Math]::Min($height-1,[int]($height*$gy/7))
            [void]$colors.Add($bitmap.GetPixel($px,$py).ToArgb())
        }
    }
    $bitmap.Dispose()
    if($colors.Count -lt 5) { throw "Installer screenshot appears blank/unrendered" }
    Write-Host ("Captured actual SARA installer UI: " + $Path)
}

$InstallerPath=(Resolve-Path $InstallerPath).Path
$parent=Split-Path $OutputPath
if($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }

$proc=Start-Process -FilePath $InstallerPath -ArgumentList "/SP-" -PassThru
try {
    $window=Find-InstallerWindow -ProcessId $proc.Id
    Capture-Window -Window $window -Path $OutputPath
}
finally {
    if(-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
}
