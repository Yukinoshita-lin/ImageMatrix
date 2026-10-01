# gui_smoke.ps1 -- GUI smoke test driven by window messages (deterministic, DPI independent).
#   * clicks "generate test pattern" (drives the ask-width / ask-height dialogs)
#   * zooms in so the pixel grid becomes visible
#   * opens the matrix text preview window
#   * exports the TXT matrix through the real save dialog
#   * screenshots the main window / preview window at every step into build\gui_out
#
#   powershell -ExecutionPolicy Bypass -File test\gui_smoke.ps1
param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$Exe    = Join-Path $Root 'build\imagematrix.exe'
$Cli    = Join-Path $Root 'build\imagematrix-cli.exe'
$OutDir = Join-Path $Root 'build\gui_out'
if (-not (Test-Path $Exe)) { Write-Host "missing $Exe"; exit 1 }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Add-Type -AssemblyName System.Drawing
$cs = @'
using System;
using System.Drawing;
using System.Text;
using System.Runtime.InteropServices;
public class Gui {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr after, string cls, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);

    public static string ClassOf(IntPtr h) { StringBuilder s = new StringBuilder(256); GetClassNameW(h, s, 256); return s.ToString(); }
    public static string TextOf(IntPtr h) { StringBuilder s = new StringBuilder(512); GetWindowTextW(h, s, 512); return s.ToString(); }
    // WM_GETTEXT works across processes, unlike GetWindowText for child controls
    public static string ControlText(IntPtr h, int max) {
        StringBuilder s = new StringBuilder(max + 1);
        SendMessageW(h, 0x000D, (IntPtr)max, s);
        return s.ToString();
    }
    // all descendants: "hwnd|class|text"
    public static string[] Descendants(IntPtr root) {
        System.Collections.Generic.List<string> list = new System.Collections.Generic.List<string>();
        EnumChildWindows(root, delegate(IntPtr h, IntPtr l) {
            list.Add(h.ToString() + "|" + ClassOf(h) + "|" + ControlText(h, 512));
            return true;
        }, IntPtr.Zero);
        return list.ToArray();
    }
    public static void Shot(IntPtr h, string path) {
        RECT r; GetWindowRect(h, out r);
        using (Bitmap bmp = new Bitmap(r.R - r.L, r.B - r.T))
        using (Graphics g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            PrintWindow(h, hdc, 2);
            g.ReleaseHdc(hdc);
            bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
        }
    }
    public static bool HasDescendant(IntPtr root, string cls) {
        bool found = false;
        EnumChildWindows(root, delegate(IntPtr h, IntPtr l) {
            if (ClassOf(h) == cls) { found = true; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    // every visible top level window of the process, as a string list "hwnd|class|title"
    public static string[] TopWindows(int pid) {
        System.Collections.Generic.List<string> list = new System.Collections.Generic.List<string>();
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == (uint)pid && IsWindowVisible(h))
                list.Add(h.ToString() + "|" + ClassOf(h) + "|" + TextOf(h));
            return true;
        }, IntPtr.Zero);
        return list.ToArray();
    }
    public static IntPtr FindTop(int pid, string cls) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == (uint)pid && IsWindowVisible(h) && ClassOf(h) == cls) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    // a dialog (#32770) that owns a ComboBoxEx32 -> the file dialog, not a message box
    public static IntPtr FindFileDialog(int pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == (uint)pid && IsWindowVisible(h) && ClassOf(h) == "#32770" && HasDescendant(h, "ComboBoxEx32")) {
                found = h; return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr FindMessageBox(int pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == (uint)pid && IsWindowVisible(h) && ClassOf(h) == "#32770" && !HasDescendant(h, "ComboBoxEx32")) {
                found = h; return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr WaitTop(int pid, string cls, int ms) {
        for (int w = 0; w < ms; w += 150) {
            IntPtr h = FindTop(pid, cls);
            if (h != IntPtr.Zero) return h;
            System.Threading.Thread.Sleep(150);
        }
        return IntPtr.Zero;
    }
    public static IntPtr FindTopByTitle(int pid, string part) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == (uint)pid && IsWindowVisible(h) && TextOf(h).Contains(part)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr WaitTopByTitle(int pid, string part, int ms) {
        for (int w = 0; w < ms; w += 150) {
            IntPtr h = FindTopByTitle(pid, part);
            if (h != IntPtr.Zero) return h;
            System.Threading.Thread.Sleep(150);
        }
        return IntPtr.Zero;
    }
    public static void WaitGone(int pid, string part, int ms) {
        for (int w = 0; w < ms; w += 150) {
            if (FindTopByTitle(pid, part) == IntPtr.Zero) return;
            System.Threading.Thread.Sleep(150);
        }
    }
}
'@
Add-Type -TypeDefinition $cs -ReferencedAssemblies System.Drawing

# control ids (must match src/gui.cpp)
$ID_GEN = 1011; $ID_TEXT = 1012; $ID_EXPORT = 1002; $ID_ZOOMIN = 1009; $ID_OPEN = 1001
$ID_ASK_OK = 1401; $ID_TXT_CLOSE = 1303
$WM_COMMAND = 0x0111; $WM_CLOSE = 0x0010; $WM_SETTEXT = 0x000C; $IDOK = 1

$fail = 0
function Step($ok, $what) {
    if ($ok) { Write-Host ("  [PASS] " + $what) -ForegroundColor Green }
    else { Write-Host ("  [FAIL] " + $what) -ForegroundColor Red; $script:fail++ }
}
function Shot($proc, $name) {
    $proc.Refresh()
    $h = $proc.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { return }
    [Gui]::Shot($h, (Join-Path $OutDir $name))
    Write-Host "  shot $name"
}
function Post($h, $id) { [Gui]::PostMessageW($h, $WM_COMMAND, [IntPtr]$id, [IntPtr]::Zero) | Out-Null }

$proc = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 3
$proc.Refresh()
$main = $proc.MainWindowHandle
Write-Host "pid=$($proc.Id) main=$main"
Shot $proc 'g1_startup.png'
Step ($main -ne [IntPtr]::Zero) "main window created"

# --- generate test pattern: two ask dialogs (width, height) ---
Post $main $ID_GEN
Start-Sleep -Milliseconds 1500
$a1 = [Gui]::WaitTop($proc.Id, 'ImageMatrixAskWnd', 5000)
Step ($a1 -ne [IntPtr]::Zero) "ask-width dialog opened"
Shot $proc 'g2_ask_dialog.png'
if ($a1 -ne [IntPtr]::Zero) { [Gui]::Shot($a1, (Join-Path $OutDir 'g2b_ask_dialog_itself.png')); Post $a1 $ID_ASK_OK }
Start-Sleep -Milliseconds 1500
$a2 = [Gui]::WaitTop($proc.Id, 'ImageMatrixAskWnd', 5000)
Step ($a2 -ne [IntPtr]::Zero) "ask-height dialog opened"
if ($a2 -ne [IntPtr]::Zero) { Post $a2 $ID_ASK_OK }
Start-Sleep -Seconds 2
Shot $proc 'g3_pattern_loaded.png'

# --- zoom in 5x so the pixel grid kicks in ---
for ($i = 0; $i -lt 5; $i++) { Post $main $ID_ZOOMIN; Start-Sleep -Milliseconds 350 }
Start-Sleep -Milliseconds 600
Shot $proc 'g4_zoomed_grid.png'

# --- matrix text preview window ---
Post $main $ID_TEXT
Start-Sleep -Milliseconds 1800
$tw = [Gui]::WaitTop($proc.Id, 'ImageMatrixTextWnd', 5000)
Step ($tw -ne [IntPtr]::Zero) "matrix text preview window opened"
if ($tw -ne [IntPtr]::Zero) {
    $editText = [Gui]::ControlText([Gui]::GetDlgItem($tw, 1300), 200000)
    Step ($editText.Contains('# ImageMatrix') -and $editText.Contains('# channel R')) "preview contains matrix text ($($editText.Length) chars)"
    [Gui]::Shot($tw, (Join-Path $OutDir 'g5_matrix_text.png'))
    Write-Host "  shot g5_matrix_text.png"
    [Gui]::PostMessageW($tw, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
}
Start-Sleep -Milliseconds 800

# --- export TXT through the real file dialog ---
$txtTarget = Join-Path $OutDir 'gui_export.txt'
if (Test-Path $txtTarget) { Remove-Item $txtTarget -Force }
Post $main $ID_EXPORT
$dlg = [Gui]::WaitTopByTitle($proc.Id, '导出数值矩阵到 TXT', 10000)
Write-Host "  save dialog: $dlg"
Step ($dlg -ne [IntPtr]::Zero) "save-file dialog opened"
Shot $proc 'g6_save_dialog.png'
if ($dlg -ne [IntPtr]::Zero) {
    # the modern file dialog nests the file name edit; search all descendants
    $ed = [IntPtr]::Zero
    foreach ($d in [Gui]::Descendants($dlg)) {
        $p = $d.Split('|')
        if ($p[1] -eq 'Edit') {
            Write-Host "    edit $($p[0]) = '$($p[2])'"
            if ($p[2] -like '*image_CHANNELS*') { $ed = [IntPtr][int]$p[0]; break }
            if ($ed -eq [IntPtr]::Zero) { $ed = [IntPtr][int]$p[0] }
        }
    }
    if ($ed -ne [IntPtr]::Zero) {
        [Gui]::SendMessageW($ed, $WM_SETTEXT, [IntPtr]::Zero, $txtTarget) | Out-Null
        Start-Sleep -Milliseconds 700
        [Gui]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
    }
    # "confirm save as" may pop up if the file already exists -> accept it
    $ov = [Gui]::WaitTopByTitle($proc.Id, '确认另存为', 2500)
    if ($ov -ne [IntPtr]::Zero) {
        [Gui]::PostMessageW($ov, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
    }
}
$mb = [Gui]::WaitTopByTitle($proc.Id, '导出成功', 12000)
if ($mb -ne [IntPtr]::Zero) {
    [Gui]::Shot($mb, (Join-Path $OutDir 'g7_export_done.png'))
    Write-Host "  shot g7_export_done.png"
    [Gui]::PostMessageW($mb, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
}
[Gui]::WaitGone($proc.Id, '导出数值矩阵到 TXT', 8000)
Start-Sleep -Milliseconds 600
Step (Test-Path $txtTarget) "TXT exported from the GUI ($(if (Test-Path $txtTarget) { (Get-Item $txtTarget).Length } else { 0 }) bytes)"

# --- the same matrix produced by the CLI must be byte identical ---
if (Test-Path $txtTarget) {
    $refPng = Join-Path $OutDir 'ref_pattern.png'
    $refTxt = Join-Path $OutDir 'ref_pattern.txt'
    & $Cli gen -o $refPng -w 320 -h 240 | Out-Null
    & $Cli to-txt -i $refPng -o $refTxt -f CHANNELS | Out-Null
    $h1 = (Get-FileHash $txtTarget -Algorithm SHA256).Hash
    $h2 = (Get-FileHash $refTxt -Algorithm SHA256).Hash
    Step ($h1 -eq $h2) "GUI export is byte identical to the CLI export (SHA256)"
    if ($h1 -ne $h2) { Write-Host "    gui=$h1"; Write-Host "    cli=$h2" }
}

# --- open an image file through the real open dialog (200x150 png) ---
$openPng = Join-Path $OutDir 'open_me.png'
& $Cli gen -o $openPng -w 200 -h 150 | Out-Null
Post $main $ID_OPEN
$dlg2 = [Gui]::WaitTopByTitle($proc.Id, '选择要识别的图片', 10000)
Step ($dlg2 -ne [IntPtr]::Zero) "open-file dialog opened"
if ($dlg2 -ne [IntPtr]::Zero) {
    $ed2 = [IntPtr]::Zero
    foreach ($d in [Gui]::Descendants($dlg2)) {
        $p = $d.Split('|')
        if ($p[1] -eq 'Edit') { $ed2 = [IntPtr][int]$p[0]; break }
    }
    if ($ed2 -ne [IntPtr]::Zero) {
        [Gui]::SendMessageW($ed2, $WM_SETTEXT, [IntPtr]::Zero, $openPng) | Out-Null
        Start-Sleep -Milliseconds 700
        [Gui]::PostMessageW($dlg2, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null
    }
}
[Gui]::WaitGone($proc.Id, '选择要识别的图片', 8000)
Start-Sleep -Milliseconds 1200
Shot $proc 'g8_image_opened.png'
# the preview must now describe the file we just opened (200x150)
Post $main $ID_TEXT
Start-Sleep -Milliseconds 1800
$tw2 = [Gui]::WaitTop($proc.Id, 'ImageMatrixTextWnd', 5000)
if ($tw2 -ne [IntPtr]::Zero) {
    $t2 = [Gui]::ControlText([Gui]::GetDlgItem($tw2, 1300), 4000)
    Step ($t2.Contains('# width 200') -and $t2.Contains('# height 150')) "image opened through the GUI (matrix header says 200x150)"
    [Gui]::PostMessageW($tw2, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
} else {
    Step $false "image opened through the GUI"
}

# --- switch the matrix format combo to GRAY (index 2) and check the preview follows ---
$cmb = [Gui]::GetDlgItem($main, 1005)
[Gui]::SendMessageW($cmb, 0x014E, [IntPtr]2, [IntPtr]::Zero) | Out-Null   # CB_SETCURSEL
$wparam = [IntPtr](1005 -bor (1 -shl 16))                                  # ID_CMB_FORMAT + CBN_SELCHANGE
[Gui]::SendMessageW($main, $WM_COMMAND, $wparam, $cmb) | Out-Null
Start-Sleep -Milliseconds 900
Post $main $ID_TEXT
Start-Sleep -Milliseconds 1800
$tw3 = [Gui]::WaitTop($proc.Id, 'ImageMatrixTextWnd', 5000)
if ($tw3 -ne [IntPtr]::Zero) {
    $t3 = [Gui]::ControlText([Gui]::GetDlgItem($tw3, 1300), 4000)
    Step ($t3.Contains('# format GRAY')) "matrix format combo switches the export format"
    Shot $proc 'g9_gray_format.png'
    [Gui]::PostMessageW($tw3, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
} else {
    Step $false "matrix format combo switches the export format"
}

Write-Host "`nremaining top level windows:"
[Gui]::TopWindows($proc.Id) | ForEach-Object { Write-Host "   $_" }

$proc.CloseMainWindow() | Out-Null
Start-Sleep -Seconds 2
if (-not $proc.HasExited) { $proc.Kill() }

if ($fail -eq 0) { Write-Host "`nGUI smoke test: ALL PASSED" -ForegroundColor Green; exit 0 }
Write-Host "`nGUI smoke test: $fail failure(s)" -ForegroundColor Red
exit 1
