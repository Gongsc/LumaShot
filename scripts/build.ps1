param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$RunTests
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio Build Tools 2022 is required.' }
$msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild was not found.' }

# The Codex desktop process can expose Path/PATH twice; normalize it for .NET Framework MSBuild.
$combinedPath = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $combinedPath, 'Process')

& $msbuild (Join-Path $repoRoot 'LumaShot.sln') /m:1 /p:Configuration=$Configuration /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($RunTests) {
    & (Join-Path $repoRoot "bin\$Configuration\LumaShot.Tests.exe")
    exit $LASTEXITCODE
}
