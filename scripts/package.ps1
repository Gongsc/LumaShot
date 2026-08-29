param(
    [string]$CertificateThumbprint = '',
    [string]$Version = '0.1.0'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$artifacts = Join-Path $repoRoot 'artifacts'
$publishDirectory = Join-Path $artifacts 'publish'
$controlCenter = Join-Path $repoRoot 'src\LumaShot.ControlCenter\LumaShot.ControlCenter.csproj'
$normalizedVersion = $Version.TrimStart('v')
$versionLabel = "v$normalizedVersion"
$standaloneExe = Join-Path $artifacts "LumaShot-$versionLabel-x64.exe"
$setupBaseName = "LumaShot-$versionLabel-Setup-x64"
$setupExe = Join-Path $artifacts "$setupBaseName.exe"

& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -RunTests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (Test-Path -LiteralPath $publishDirectory) {
    Remove-Item -LiteralPath $publishDirectory -Recurse -Force
}
New-Item -ItemType Directory -Force $publishDirectory | Out-Null
& dotnet publish $controlCenter `
    --configuration Release `
    --runtime win-x64 `
    --self-contained true `
    --output $publishDirectory `
    -p:Platform=x64 `
    -p:ContinuousIntegrationBuild=true `
    -p:Version=$normalizedVersion
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$publishedExe = Join-Path $publishDirectory 'LumaShot.exe'
if (-not (Test-Path -LiteralPath $publishedExe)) { throw 'The unified standalone executable was not produced.' }

$verificationRoot = Join-Path ([IO.Path]::GetTempPath()) "LumaShot-runtime-$([Guid]::NewGuid().ToString('N'))"
try {
    $env:LUMASHOT_RUNTIME_ROOT = $verificationRoot
    $verification = Start-Process `
        -FilePath $publishedExe `
        -ArgumentList '--verify-bundle' `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($verification.ExitCode -ne 0) { throw "Bundle verification failed with exit code $($verification.ExitCode)." }

    $extractedBackend = Get-ChildItem -LiteralPath $verificationRoot -Filter 'LumaShot.Native.exe' -Recurse -File |
        Select-Object -First 1
    if (-not $extractedBackend) { throw 'The embedded native screenshot engine was not extracted.' }
}
finally {
    Remove-Item Env:LUMASHOT_RUNTIME_ROOT -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $verificationRoot) {
        Remove-Item -LiteralPath $verificationRoot -Recurse -Force
    }
}

if ($CertificateThumbprint) {
    & signtool sign /sha1 $CertificateThumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $publishedExe
    if ($LASTEXITCODE -ne 0) { throw 'Authenticode signing failed.' }
}
Copy-Item -LiteralPath $publishedExe -Destination $standaloneExe -Force

$legacyTargets = @(
    (Join-Path $artifacts 'LumaShot-portable-x64'),
    (Join-Path $artifacts 'LumaShot-portable-x64.zip')
)
foreach ($legacyTarget in $legacyTargets) {
    if (Test-Path -LiteralPath $legacyTarget) {
        Remove-Item -LiteralPath $legacyTarget -Recurse -Force
    }
}

$iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
$isccPath = if ($iscc) { $iscc.Source } else { '' }
if (-not $isccPath) {
    $knownLocations = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )
    $isccPath = $knownLocations | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}

if ($isccPath) {
    & $isccPath "/DMyAppVersion=$normalizedVersion" "/DMyOutputBaseFilename=$setupBaseName" (Join-Path $repoRoot 'installer\LumaShot.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
    if ($CertificateThumbprint) {
        & signtool sign /sha1 $CertificateThumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $setupExe
        if ($LASTEXITCODE -ne 0) { throw 'Installer signing failed.' }
    }
}
else {
    Write-Warning 'Inno Setup was not found; the standalone executable was created, but the installer was skipped.'
}

if (Test-Path -LiteralPath $publishDirectory) {
    Remove-Item -LiteralPath $publishDirectory -Recurse -Force
}

Write-Output "Standalone: $standaloneExe"
if (Test-Path -LiteralPath $setupExe) { Write-Output "Installer: $setupExe" }
