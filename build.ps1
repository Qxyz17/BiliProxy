$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$src  = Join-Path $root 'src'
$obj  = Join-Path $root 'build'
$out  = Join-Path $obj 'bp_link.exe'
$exe  = Join-Path $root 'bin\BiliProxy.exe'

New-Item -ItemType Directory -Force $obj, (Join-Path $root 'bin') | Out-Null

$msysBin = if ($env:MSYS2_BIN) { $env:MSYS2_BIN } else { 'C:\msys64\mingw64\bin' }
$env:Path = "$msysBin;" + $env:Path
$gpp  = Join-Path $msysBin 'g++.exe'
$objcopy = Join-Path $msysBin 'objcopy.exe'
$units = @('common','cert','config','http','playurl','proxy','gui','main','overlay','transcode','ffmpeg_embed','download')
$libs = @('-lssl','-lcrypto','-lz','-lbrotlidec','-lbrotlicommon',
          '-lws2_32','-lcrypt32','-lwininet','-lsecur32','-lole32','-lshell32','-luser32','-ladvapi32','-lgdiplus','-lgdi32','-ld3d11','-ld2d1','-ldcomp','-ldxgi','-ldxguid','-lwindowscodecs','-luuid','-lcomdlg32')

# ---------------------------------------------------------------- 鍐呭祵 ffmpeg
$ffRaw = Join-Path $root 'third_party\ffmpeg.exe'
$ffDef = Join-Path $obj 'ffmpeg.deflate'
$ffObj = Join-Path $obj 'ffmpeg_blob.o'

if (Test-Path $ffRaw) {
    $need = $true
    if (Test-Path $ffDef) {
        if ((Get-Item $ffDef).LastWriteTime -ge (Get-Item $ffRaw).LastWriteTime) { $need = $false }
    }
    if ($need) {
        Write-Host '== pack ffmpeg =='
        python (Join-Path $root 'tools\pack_ffmpeg.py')
        if ($LASTEXITCODE -ne 0) { throw 'pack ffmpeg failed' }
    }
}

if (Test-Path $ffDef) {
    Write-Host '== embed ffmpeg =='
    Push-Location $obj
    & $objcopy -I binary -O pe-x86-64 -B i386:x86-64 'ffmpeg.deflate' 'ffmpeg_blob.o'
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw 'objcopy failed' }
    Pop-Location
} else {
    Write-Host 'warn: no ffmpeg blob; transcoding will look for ffmpeg.exe next to the exe'
}

# ---------------------------------------------------------------- 缂栬瘧
Write-Host '== compile =='
$objs = @()
foreach ($u in $units) {
    $o = Join-Path $obj "$u.o"
    & $gpp -O2 -std=c++17 -Wall -municode -c (Join-Path $src "$u.cpp") -o $o
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $u" }
    $objs += $o
}
if (Test-Path $ffObj) { $objs += $ffObj }

Write-Host '== link gui =='
& $gpp -O2 -std=c++17 -municode -static -static-libgcc -static-libstdc++ @objs -o $out '-Wl,--start-group' @libs '-Wl,--end-group' -mwindows -pthread
if ($LASTEXITCODE -ne 0) { throw 'link failed' }
Copy-Item -Force $out $exe

Write-Host '== link console =='
$cexe = Join-Path $root 'bin\BiliProxy-console.exe'
& $gpp -O2 -std=c++17 -municode -static -static-libgcc -static-libstdc++ @objs -o $cexe '-Wl,--start-group' @libs '-Wl,--end-group' -mconsole -pthread
if ($LASTEXITCODE -ne 0) { throw 'console link failed' }

Write-Host ('built: {0} ({1:N0} bytes)' -f $exe, (Get-Item $exe).Length)

# 涓嶅啀寰€ bin 鏀剧嫭绔?ffmpeg.exe 鈥斺€?宸茬粡鍐呯疆杩?exe
$stale = Join-Path $root 'bin\ffmpeg.exe'
if (Test-Path $stale) { Remove-Item -Force $stale; Write-Host 'removed stale bin\ffmpeg.exe' }

