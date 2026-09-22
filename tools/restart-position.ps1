param([int]$Cycles=3,[int]$SettleSeconds=12)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskExe=Join-Path $taskRoot 'out\DeskPerch.exe'
$taskProbe=Join-Path $taskRoot 'build\Release\DeskPerchProbe.exe'
$taskSettings=Join-Path $env:LOCALAPPDATA 'DeskPerch\settings.dat'
$taskPrefs=(Get-Content -LiteralPath $taskSettings)[1] -split ' '
if($taskPrefs[1] -ne '1' -or $taskPrefs[2] -ne '1'){throw 'Run with the widget locked and visible'}
function Position {
    $taskState=((& $taskProbe --window) -join "`n")
    $taskMatch=[regex]::Match($taskState,'rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)')
    if(-not $taskMatch.Success){throw 'No widget rectangle'}
    return $taskMatch.Groups[1].Value+','+$taskMatch.Groups[2].Value
}
$taskSaved=Get-Content -LiteralPath $taskSettings -Raw
Start-Sleep -Seconds $SettleSeconds
$taskAnchor=Position
for($taskCycle=1;$taskCycle -le $Cycles;$taskCycle++) {
    $taskProcess=Get-Process DeskPerch
    Start-Process $taskExe -ArgumentList '--control','exit' -WindowStyle Hidden -Wait
    if(-not $taskProcess.WaitForExit(6000)){throw 'Exit timed out'}
    Start-Process $taskExe -WindowStyle Hidden
    Start-Sleep -Seconds $SettleSeconds
    $taskRestored=Position
    if($taskRestored -ne $taskAnchor){throw "Position drift: $taskAnchor -> $taskRestored"}
    if((Get-Content -LiteralPath $taskSettings -Raw) -ne $taskSaved){throw 'Restart changed saved preferences'}
    Write-Output "Restart $taskCycle passed: $taskRestored"
}
