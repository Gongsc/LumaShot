param([string]$CertificateThumbprint = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -RunTests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$artifacts = Join-Path $repoRoot 'artifacts'
$portable = Join-Path $artifacts 'LumaShot-portable-x64'
New-Item -ItemType Directory -Force $portable | Out-Null
$releaseExe = Join-Path $repoRoot 'bin\Release\LumaShot.exe'
if ($CertificateThumbprint) {
    & signtool sign /sha1 $CertificateThumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $releaseExe
    if ($LASTEXITCODE -ne 0) { throw 'Authenticode signing failed.' }
}
Copy-Item $releaseExe $portable -Force
Copy-Item (Join-Path $repoRoot 'README.md') $portable -Force
Copy-Item (Join-Path $repoRoot 'README.zh-CN.md') $portable -Force
Copy-Item (Join-Path $repoRoot 'docs') $portable -Recurse -Force
Compress-Archive -Path (Join-Path $portable '*') -DestinationPath (Join-Path $artifacts 'LumaShot-portable-x64.zip') -Force
$iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
$isccPath = if ($iscc) { $iscc.Source } else { '' }
if (-not $iscc) {
    $knownLocations = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )
    $isccPath = $knownLocations | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if ($isccPath) {
    & $isccPath (Join-Path $repoRoot 'installer\LumaShot.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
}
else { Write-Warning 'Inno Setup was not found; portable package was created, installer was skipped.' }
