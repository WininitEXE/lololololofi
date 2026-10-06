param([string]$Exe, [string]$Args, [string]$Out, [int]$WaitSec = 4)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinCap {
  [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$p = Start-Process -FilePath $Exe -ArgumentList $Args -PassThru
Start-Sleep -Seconds $WaitSec
$h = [WinCap]::FindWindow("OneBitConverterWindow", $null)
if ($h -eq [IntPtr]::Zero) { Write-Output "window not found"; Stop-Process -Id $p.Id -Force; exit 1 }
$r = New-Object WinCap+RECT
[void][WinCap]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
Write-Output ("window " + $w + "x" + $ht + " visible=" + [WinCap]::IsWindowVisible($h))
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [WinCap]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ("printwindow=" + $ok + " saved " + $Out)
Stop-Process -Id $p.Id -Force
