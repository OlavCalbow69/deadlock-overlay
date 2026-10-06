param([string]$DotnetPath = '')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path $PSScriptRoot -Parent
$releaseDirectory = Join-Path $projectDirectory 'build\Release'
$helperDirectory = Join-Path $releaseDirectory 'tools\data-update'
$dumperDirectory = Join-Path $releaseDirectory 'tools\schema-dumper'
if (!$DotnetPath) {
    if ($env:DEADLOCK_DOTNET) { $DotnetPath = $env:DEADLOCK_DOTNET }
    else {
        $dotnetCommand = Get-Command dotnet -ErrorAction SilentlyContinue
        if ($dotnetCommand) { $DotnetPath = $dotnetCommand.Source }
    }
}
if (!$DotnetPath -or !(Test-Path -LiteralPath $DotnetPath)) {
    throw 'A .NET 10 SDK is needed to rebuild data tools. Use -DotnetPath <dotnet.exe> or set DEADLOCK_DOTNET. The packaged updater itself needs no SDK.'
}
& $DotnetPath publish (Join-Path $PSScriptRoot 'data-update\DataUpdate.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:DebugType=None -o $helperDirectory
if ($LASTEXITCODE -ne 0) { throw 'Data updater publish failed.' }
& (Join-Path $helperDirectory 'DeadlockDataUpdate.exe') --self-test
if ($LASTEXITCODE -ne 0) { throw 'Data updater checks failed.' }
$dumperSource = Join-Path $PSScriptRoot 'schema-dumper\source'
$dumperBuild = Join-Path $PSScriptRoot 'schema-dumper\build'
cmake -S $dumperSource -B $dumperBuild -A x64
if ($LASTEXITCODE -ne 0) { throw 'Schema dumper configuration failed.' }
cmake --build $dumperBuild --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Schema dumper build failed.' }
New-Item -ItemType Directory -Path $dumperDirectory -Force | Out-Null
foreach ($name in @('dezlock-dump.exe','dezlock-worker.dll','patterns.json','sdk-cherry-pick.json')) {
    Copy-Item -LiteralPath (Join-Path $dumperBuild ('bin\Release\' + $name)) -Destination $dumperDirectory -Force
    Copy-Item -LiteralPath (Join-Path $dumperDirectory $name) -Destination (Join-Path $PSScriptRoot 'schema-dumper') -Force
}
foreach ($file in Get-ChildItem -LiteralPath $helperDirectory -File | Where-Object Extension -ne '.pdb') {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $PSScriptRoot 'data-update') -Force
}
Write-Output 'Bundled map/schema updater is ready.'
