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

# A host process can expose Path/PATH twice. .NET Framework MSBuild then fails
# while constructing the environment for CL.exe, so launch it from a clean
# machine environment instead of forwarding the malformed inherited block.
$solution = Join-Path $repoRoot 'LumaShot.sln'
$arguments = @("`"$solution`"", '/m:1', "/p:Configuration=$Configuration", '/p:Platform=x64', '/v:minimal')
$build = Start-Process -FilePath $msbuild -ArgumentList $arguments -UseNewEnvironment -NoNewWindow -PassThru
$build.WaitForExit()
if ($build.ExitCode -ne 0) { exit $build.ExitCode }

$controlCenter = Join-Path $repoRoot 'src\LumaShot.ControlCenter\LumaShot.ControlCenter.csproj'
& dotnet build $controlCenter `
    --configuration $Configuration `
    --runtime win-x64 `
    -p:Platform=x64 `
    --verbosity minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($RunTests) {
    & (Join-Path $repoRoot "bin\$Configuration\LumaShot.Tests.exe")
    exit $LASTEXITCODE
}
