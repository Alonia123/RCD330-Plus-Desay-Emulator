param([string]$Out = "$PSScriptRoot\hmi.png", [string]$Title = "Weston")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public struct RECT { public int L, T, R, B; }
}
"@
[W]::SetProcessDPIAware() | Out-Null
$found = $null
[W]::EnumWindows({ param($h, $l)
  $sb = New-Object System.Text.StringBuilder 256
  [W]::GetWindowText($h, $sb, 256) | Out-Null
  if ([W]::IsWindowVisible($h) -and $sb.ToString() -like "*$Title*") { $script:found = $h; $script:name = $sb.ToString() }
  return $true }, [IntPtr]::Zero) | Out-Null
if (-not $found) { Write-Output "Kein Fenster mit '$Title' gefunden"; exit 1 }
[W]::SetForegroundWindow($found) | Out-Null
Start-Sleep -Milliseconds 700
$r = New-Object W+RECT
[W]::GetWindowRect($found, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "Fenster '$name' ${w}x${h} -> $Out"
