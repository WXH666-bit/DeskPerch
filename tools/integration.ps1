param([int]$Cycles=100)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskExe=Join-Path $taskRoot 'out\DeskPerch.exe'
$taskProbe=Join-Path $taskRoot 'build\Release\DeskPerchProbe.exe'
$taskOutput=Join-Path $taskRoot 'artifacts\integration'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
function Command([string]$value) {
    $taskCommand=Start-Process -FilePath $taskExe -ArgumentList '--control',$value -WindowStyle Hidden -Wait -PassThru
    if($taskCommand.ExitCode -ne 0){throw "Command failed: $value ($($taskCommand.ExitCode))"}
}
function Window-State { return ((& $taskProbe --window) -join "`n") }
function Assert([bool]$ok,[string]$message){if(-not $ok){throw $message}}
$taskInitial=(Get-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'DeskPerch\settings.dat'))[1] -split ' '
Assert ($taskInitial[0] -eq '0' -and $taskInitial[2] -eq '1') 'Start integration test in full visible mode'
$taskBefore=Window-State
$taskBefore | Set-Content -LiteralPath (Join-Path $taskOutput 'full.txt') -Encoding utf8
Assert ($taskBefore -match 'parent=SHELLDLL_DefView visible=1 child=1 no_activate=1 tool_window=1 topmost=0') 'Desktop child flags incorrect'
Assert ($taskBefore -match 'app_has_foreground=0') 'App unexpectedly has foreground'
Start-Process -FilePath $taskExe -WindowStyle Hidden -Wait
Assert (@(Get-Process DeskPerch).Count -eq 1) 'Duplicate instance created'
Command compact
$taskCompact=Window-State
$taskCompact | Set-Content -LiteralPath (Join-Path $taskOutput 'compact.txt') -Encoding utf8
$taskA=[regex]::Match($taskBefore,'rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)')
$taskB=[regex]::Match($taskCompact,'rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)')
Assert ($taskA.Groups[1].Value -eq $taskB.Groups[1].Value -and $taskA.Groups[2].Value -eq $taskB.Groups[2].Value) 'Mode switch moved top-left corner'
Assert (([int]$taskB.Groups[4].Value-[int]$taskB.Groups[2].Value) -lt ([int]$taskA.Groups[4].Value-[int]$taskA.Groups[2].Value)) 'Compact mode did not shrink'
Command compact
Command visibility
$taskHidden=Window-State
$taskHidden | Set-Content -LiteralPath (Join-Path $taskOutput 'hidden.txt') -Encoding utf8
Assert ($taskHidden -match 'visible=0') 'Hide failed'
Command visibility
Assert ((Window-State) -match 'visible=1') 'Show failed'
Command lock
$taskLocked=(Get-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'DeskPerch\settings.dat'))[1] -split ' '
Assert ($taskLocked[1] -ne $taskInitial[1]) 'Lock setting not saved'
Command lock
$taskRows=[Collections.Generic.List[object]]::new()
$taskApp=Get-Process DeskPerch
$taskCpuStart=$taskApp.TotalProcessorTime.TotalSeconds
$taskClock=[Diagnostics.Stopwatch]::StartNew()
for($taskIndex=0;$taskIndex -le $Cycles;$taskIndex++) {
    $taskApp.Refresh()
    $taskRows.Add([pscustomobject]@{Cycle=$taskIndex;Elapsed=$taskClock.Elapsed.TotalSeconds;WorkingSetMiB=$taskApp.WorkingSet64/1MB;PrivateMiB=$taskApp.PrivateMemorySize64/1MB;Handles=$taskApp.HandleCount;Threads=$taskApp.Threads.Count})
    if($taskIndex -eq $Cycles){break}
    Command compact;Command compact;Command visibility;Command visibility;Command lock;Command lock
}
$taskRows | Export-Csv -LiteralPath (Join-Path $taskOutput 'stress.csv') -NoTypeInformation -Encoding utf8
$taskDuration=$taskClock.Elapsed.TotalSeconds
$taskApp.Refresh()
$taskCpu=100*($taskApp.TotalProcessorTime.TotalSeconds-$taskCpuStart)/($taskDuration*[Environment]::ProcessorCount)
$taskState=Window-State
$taskState | Set-Content -LiteralPath (Join-Path $taskOutput 'after-stress.txt') -Encoding utf8
Assert ($taskState -match 'app_has_foreground=0') 'Stress acquired foreground'
$taskSaved=Get-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'DeskPerch\settings.dat') -Raw
$taskId=$taskApp.Id
Command exit
Assert ($taskApp.WaitForExit(6000)) 'Exit timed out'
Assert (@(Get-Process DeskPerch -ErrorAction SilentlyContinue).Count -eq 0) 'App survived exit'
Start-Process -FilePath $taskExe -WindowStyle Hidden
Start-Sleep -Seconds 1
$taskRestored=Get-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'DeskPerch\settings.dat') -Raw
Assert ($taskRestored -eq $taskSaved) 'Settings not preserved over restart'
@{Status='passed';Cycles=$Cycles;Operations=$Cycles*6;DurationSeconds=$taskDuration;CpuPercentMean=$taskCpu;First=$taskRows[0];Last=$taskRows[-1];PeakWorkingSetMiB=($taskRows.WorkingSetMiB | Measure-Object -Maximum).Maximum;PeakPrivateMiB=($taskRows.PrivateMiB | Measure-Object -Maximum).Maximum;Scope='Native command/window-state integration; no simulated hardware events or injected desktop input'} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskOutput 'summary.json') -Encoding utf8
