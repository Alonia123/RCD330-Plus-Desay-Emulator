param([int]$X, [int]$Y, [string]$Title = "top-level window")

Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class C {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public struct RECT { public int L, T, R, B; }
}
"@
[C]::SetProcessDPIAware() | Out-Null
$found = $null
[C]::EnumWindows({ param($h, $l)
  $sb = New-Object System.Text.StringBuilder 256
  [C]::GetWindowText($h, $sb, 256) | Out-Null
  if ([C]::IsWindowVisible($h) -and $sb.ToString() -like "*$Title*") { $script:found = $h }
  return $true }, [IntPtr]::Zero) | Out-Null
if (-not $found) { Write-Output "Fenster nicht gefunden"; exit 1 }
[C]::SetForegroundWindow($found) | Out-Null
Start-Sleep -Milliseconds 300
$r = New-Object C+RECT
[C]::GetWindowRect($found, [ref]$r) | Out-Null
[C]::SetCursorPos($r.L + $X, $r.T + $Y) | Out-Null
Start-Sleep -Milliseconds 150
[C]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
Start-Sleep -Milliseconds 120
[C]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
Write-Output "Klick bei Fenster+($X,$Y) = Bildschirm ($($r.L + $X),$($r.T + $Y))"
