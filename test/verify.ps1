# verify.ps1 -- 独立验证脚本（不依赖程序自身的编解码器做对照）
#
#   方向一：手工构造一张 4x3 的 BMP（已知每个像素的 R/G/B）-> 程序导出 CHANNELS 矩阵
#           -> 逐项核对 TXT 中的 R/G/B/灰度 数值是否符合预期
#   方向二：手工编写一个 TXT 矩阵 -> 程序还原成 BMP -> 直接解析 BMP 字节
#           -> 逐像素核对是否与 TXT 中的数值一致
#
# 用法： powershell -ExecutionPolicy Bypass -File test\verify.ps1
#        （需先执行 build.bat 生成 build\imagematrix-cli.exe）

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $root) { $root = (Get-Location).Path }
$cli  = Join-Path $root 'build\imagematrix-cli.exe'
$dir  = Join-Path $root 'build\verify_out'
if (-not (Test-Path $cli)) { Write-Host "找不到 $cli ，请先运行 build.bat" -ForegroundColor Red; exit 1 }
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$fail = 0
function Check($ok, $what, $detail) {
    if ($ok) { Write-Host ("  [PASS] " + $what) -ForegroundColor Green }
    else { Write-Host ("  [FAIL] " + $what + "  -- " + $detail) -ForegroundColor Red; $script:fail++ }
}

# 期望的像素：3 行 x 4 列，每项为 @(R,G,B)
$px = @(
    @(@(10,20,30),  @(40,50,60),   @(70,80,90),   @(100,110,120)),
    @(@(130,140,150),@(160,170,180),@(190,200,210),@(220,230,240)),
    @(@(255,0,0),   @(0,255,0),    @(0,0,255),    @(255,255,255))
)
$W = 4; $H = 3

function WriteBmp24($path, $pixels, $w, $h) {
    $stride = [int]([math]::Ceiling($w * 3 / 4) * 4)
    $datasize = $stride * $h
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    $bw.Write([byte][char]'B'); $bw.Write([byte][char]'M')
    $bw.Write([int](54 + $datasize)); $bw.Write([int]0); $bw.Write([int]54)
    $bw.Write([int]40); $bw.Write([int]$w); $bw.Write([int]$h)
    $bw.Write([int16]1); $bw.Write([int16]24)
    $bw.Write([int]0); $bw.Write([int]$datasize)
    $bw.Write([int]2835); $bw.Write([int]2835); $bw.Write([int]0); $bw.Write([int]0)
    for ($y = $h - 1; $y -ge 0; $y--) {                 # BMP 自下而上
        for ($x = 0; $x -lt $w; $x++) {
            $p = $pixels[$y][$x]
            $bw.Write([byte]$p[2]); $bw.Write([byte]$p[1]); $bw.Write([byte]$p[0])
        }
        for ($k = 0; $k -lt ($stride - $w * 3); $k++) { $bw.Write([byte]0) }
    }
    $bw.Flush()
    [System.IO.File]::WriteAllBytes($path, $ms.ToArray())
    $bw.Dispose(); $ms.Dispose()
}

function ReadBmp24($path) {
    $b = [System.IO.File]::ReadAllBytes($path)
    if ($b[0] -ne [byte][char]'B' -or $b[1] -ne [byte][char]'M') { throw "不是 BMP 文件: $path" }
    $off    = [BitConverter]::ToInt32($b, 10)
    $w      = [BitConverter]::ToInt32($b, 18)
    $h      = [BitConverter]::ToInt32($b, 22)
    $bottom = $h -gt 0
    if (-not $bottom) { $h = -$h }
    $bpp    = [BitConverter]::ToUInt16($b, 28)
    if ($bpp -ne 24) { throw "本脚本只解析 24 位 BMP（实际 $bpp 位）" }
    $stride = [int]([math]::Ceiling($w * 3 / 4) * 4)
    $out = @()
    for ($y = 0; $y -lt $h; $y++) {
        $src = if ($bottom) { $h - 1 - $y } else { $y }
        $row = @()
        for ($x = 0; $x -lt $w; $x++) {
            $i = $off + $src * $stride + $x * 3
            $row += ,@([int]$b[$i + 2], [int]$b[$i + 1], [int]$b[$i])
        }
        $out += ,$row
    }
    return @{ W = $w; H = $h; Px = $out }
}

function Gray($p) { return [int][math]::Round(0.299 * $p[0] + 0.587 * $p[1] + 0.114 * $p[2], 0, [MidpointRounding]::AwayFromZero) }

Write-Host "`n== 方向一：BMP 图片 -> TXT 矩阵 ==" -ForegroundColor Cyan
$bmp = Join-Path $dir 'hand.bmp'
WriteBmp24 $bmp $px $W $H
& $cli to-txt -i $bmp -o (Join-Path $dir 'hand.txt') -f CHANNELS | Out-Null
$txtLines = Get-Content (Join-Path $dir 'hand.txt')
$chunks = @{}; $cur = ''
foreach ($ln in $txtLines) {
    $t = $ln.Trim()
    if ($t -like '# channel *') { $cur = ($t -split '\s+')[2]; $chunks[$cur] = @(); continue }
    if ($t -eq '' -or $t.StartsWith('#')) { continue }
    if ($cur -ne '') { $chunks[$cur] += ,($t -split '\s+' | ForEach-Object { [int]$_ }) }
}
Check ($chunks.Keys.Count -eq 4) "TXT 中包含 R/G/B/GRAY 四个矩阵" ("实际: " + ($chunks.Keys -join ','))
$bad = 0; $detail = ''
foreach ($ch in 'R','G','B','GRAY') {
    $idx = @{R=0; G=1; B=2}[$ch]
    for ($y = 0; $y -lt $H; $y++) {
        for ($x = 0; $x -lt $W; $x++) {
            $got = $chunks[$ch][$y][$x]
            $want = if ($ch -eq 'GRAY') { Gray $px[$y][$x] } else { $px[$y][$x][$idx] }
            if ($got -ne $want) { $bad++; if ($detail -eq '') { $detail = "$ch($x,$y) 期望 $want 实际 $got" } }
        }
    }
}
Check ($bad -eq 0) "48 个 R/G/B 值与 12 个灰度值全部与原始像素一致" $detail

Write-Host "`n== 方向二：TXT 矩阵 -> BMP 图片 ==" -ForegroundColor Cyan
$mtx = Join-Path $dir 'hand_written.txt'
$lines = @('# width 4', '# height 3', '# format CHANNELS', '# max 255')
foreach ($ch in 'R','G','B') {
    $idx = @{R=0; G=1; B=2}[$ch]
    $lines += "# channel $ch"
    for ($y = 0; $y -lt $H; $y++) {
        $lines += (($px[$y] | ForEach-Object { $_[$idx] }) -join ' ')
    }
}
Set-Content -Path $mtx -Value $lines -Encoding ascii
$out2 = Join-Path $dir 'from_txt.bmp'
& $cli to-image -i $mtx -o $out2 | Out-Null
$img = ReadBmp24 $out2
Check (($img.W -eq $W) -and ($img.H -eq $H)) "还原出的图片尺寸为 4x3" ("实际: $($img.W)x$($img.H)")
$bad2 = 0; $detail2 = ''
for ($y = 0; $y -lt $H; $y++) {
    for ($x = 0; $x -lt $W; $x++) {
        $got = $img.Px[$y][$x]; $want = $px[$y][$x]
        if (($got[0] -ne $want[0]) -or ($got[1] -ne $want[1]) -or ($got[2] -ne $want[2])) {
            $bad2++
            if ($detail2 -eq '') { $detail2 = "($x,$y) want R$($want[0]) G$($want[1]) B$($want[2]), got R$($got[0]) G$($got[1]) B$($got[2])" }
        }
    }
}
Check ($bad2 -eq 0) "12 个像素的 R/G/B 全部与 TXT 数值一致" $detail2

Write-Host "`n== 方向三：TXT 矩阵 -> 图片 -> TXT 矩阵（灰度矩阵往返）==" -ForegroundColor Cyan
$grayTxt = Join-Path $dir 'gray_only.txt'
Set-Content -Path $grayTxt -Encoding ascii -Value @('# width 3', '# height 2', '# format GRAY', '0 128 255', '64 200 33')
$gimg = Join-Path $dir 'gray_from_txt.png'
& $cli to-image -i $grayTxt -o $gimg | Out-Null
& $cli to-txt -i $gimg -o (Join-Path $dir 'gray_back.txt') -f GRAY | Out-Null
$backRows = Get-Content (Join-Path $dir 'gray_back.txt') | Where-Object { $_ -notlike '#*' -and $_.Trim() -ne '' }
$wantRows = @('0 128 255', '64 200 33')
Check (($backRows.Count -eq 2) -and ($backRows[0].Trim() -eq $wantRows[0]) -and ($backRows[1].Trim() -eq $wantRows[1])) `
      "灰度矩阵经 PNG 往返后数值不变" ("实际: " + ($backRows -join ' | '))

Write-Host ""
if ($fail -eq 0) { Write-Host "全部通过（0 个失败）" -ForegroundColor Green; exit 0 }
Write-Host "$fail 项失败" -ForegroundColor Red
exit 1
