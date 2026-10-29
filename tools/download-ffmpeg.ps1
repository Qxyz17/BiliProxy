param(
  [string]$Url = 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip',
  [string]$Out = (Join-Path $PSScriptRoot '..\third_party\ffmpeg.zip')
)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
[Net.ServicePointManager]::DefaultConnectionLimit = 8
$req = [System.Net.HttpWebRequest]::Create($Url)
$req.Proxy = $null
$req.Timeout = 60000
$req.ReadWriteTimeout = 120000
$req.UserAgent = 'Mozilla/5.0'
$resp = $req.GetResponse()
$total = $resp.ContentLength
Write-Host "content-length: $total"
$rs = $resp.GetResponseStream()
$fs = [System.IO.File]::Create($Out)
$buf = New-Object byte[] 1048576
$done = 0
$sw = [Diagnostics.Stopwatch]::StartNew()
while (($n = $rs.Read($buf, 0, $buf.Length)) -gt 0) {
    $fs.Write($buf, 0, $n)
    $done += $n
    if ($sw.Elapsed.TotalSeconds -ge 5) {
        Write-Host ("{0:N1} MB ({1:N0}%)" -f ($done / 1MB), (100.0 * $done / [Math]::Max(1, $total)))
        $sw.Restart()
    }
}
$fs.Close(); $rs.Close(); $resp.Close()
Write-Host ("done: {0:N0} bytes" -f $done)
