param(
    [Parameter(Mandatory)][ValidatePattern('^(v?\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?|dev-[0-9a-f]{7,40})$')][string]$Version,
    [string]$BuildDirectory = 'build'
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskExe = Join-Path $taskRoot "$BuildDirectory\Release\DeskPerch.exe"
if (-not (Test-Path -LiteralPath $taskExe)) { throw 'Build Release before packaging.' }
$taskDist = Join-Path $taskRoot 'dist'
$taskStage = Join-Path $taskDist "DeskPerch-$Version-win-x64"
if (Test-Path -LiteralPath $taskStage) { throw "Package staging already exists: $taskStage; use a clean build directory." }
New-Item -ItemType Directory -Path (Join-Path $taskStage 'assets'), (Join-Path $taskStage 'docs') -Force | Out-Null
Copy-Item -LiteralPath $taskExe -Destination (Join-Path $taskStage 'DeskPerch.exe')
foreach ($taskReadme in 'README.md','README.en.md') {
    Copy-Item -LiteralPath (Join-Path $taskRoot $taskReadme) -Destination $taskStage
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'docs\SUPPORT.md') -Destination (Join-Path $taskStage 'docs')
foreach ($taskAsset in 'deskperch.svg','deskperch.png','deskperch.ico') {
    Copy-Item -LiteralPath (Join-Path $taskRoot "assets\$taskAsset") -Destination (Join-Path $taskStage 'assets')
}
$taskZip = Join-Path $taskDist "DeskPerch-$Version-win-x64.zip"
Compress-Archive -LiteralPath $taskStage -DestinationPath $taskZip
Copy-Item -LiteralPath $taskExe -Destination (Join-Path $taskDist 'DeskPerch.exe')
$taskSums = foreach ($taskAsset in @($taskZip, (Join-Path $taskDist 'DeskPerch.exe'))) {
    $taskHash = Get-FileHash -LiteralPath $taskAsset -Algorithm SHA256
    '{0}  {1}' -f $taskHash.Hash.ToLowerInvariant(), (Split-Path $taskAsset -Leaf)
}
($taskSums -join "`n") + "`n" | Set-Content -LiteralPath (Join-Path $taskDist 'SHA256SUMS.txt') -Encoding ascii -NoNewline
Write-Output "Packaged $taskZip"
