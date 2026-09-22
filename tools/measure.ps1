param(
    [int]$ProcessId = 0,
    [int]$WarmupSeconds = 300,
    [int]$SecondsPerMode = 600,
    [int]$SoakSeconds = 0,
    [int]$SampleSeconds = 1,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\artifacts\performance')
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$taskOutput = (Resolve-Path -LiteralPath $OutputDirectory).Path
if(-not $ProcessId) {
    $taskCandidates = @(Get-Process DeskPerch -ErrorAction Stop)
    if($taskCandidates.Count -ne 1){throw 'Expected exactly one DeskPerch process'}
    $ProcessId = $taskCandidates[0].Id
}
$taskProcess = Get-Process -Id $ProcessId
$taskExe = $taskProcess.Path
$taskStartTime = $taskProcess.StartTime
$taskCores = [Environment]::ProcessorCount
$taskConfig = Join-Path $env:LOCALAPPDATA 'DeskPerch\settings.dat'
function Read-Mode {
    $taskFields = (Get-Content -LiteralPath $taskConfig -Encoding utf8)[1] -split ' '
    return @{Compact=($taskFields[0] -eq '1'); Visible=($taskFields[2] -eq '1')}
}
function Set-Mode([bool]$compact,[bool]$visible) {
    $taskMode = Read-Mode
    if($taskMode.Compact -ne $compact){Start-Process -FilePath $taskExe -ArgumentList '--control','compact' -WindowStyle Hidden -Wait}
    if($taskMode.Visible -ne $visible){Start-Process -FilePath $taskExe -ArgumentList '--control','visibility' -WindowStyle Hidden -Wait}
}
function Check-Process {
    $taskCurrent = Get-Process -Id $ProcessId -ErrorAction Stop
    if($taskCurrent.StartTime -ne $taskStartTime){throw 'Process was restarted; sample series ended'}
    return $taskCurrent
}
function Measure-Stage([string]$name,[int]$seconds,[int]$interval) {
    $taskFile = Join-Path $taskOutput ($name+'.csv')
    $taskWriter = [IO.StreamWriter]::new($taskFile,$false,[Text.UTF8Encoding]::new($false))
    $taskWriter.WriteLine('Utc,ElapsedSeconds,WorkingSetMiB,PrivateMiB,CpuPercent,Handles,Threads,DwmCpuPercent,GapSeconds')
    $taskClock = [Diagnostics.Stopwatch]::StartNew()
    $taskPrev = Check-Process
    $taskPrevCpu = $taskPrev.TotalProcessorTime.TotalSeconds
    $taskPrevTick = $taskClock.Elapsed.TotalSeconds
    $taskDwm = Get-Process dwm -ErrorAction SilentlyContinue | Where-Object SessionId -eq $taskProcess.SessionId | Select-Object -First 1
    $taskDwmCpu = if($taskDwm){$taskDwm.TotalProcessorTime.TotalSeconds}else{0}
    $taskRows = [Collections.Generic.List[object]]::new()
    try {
        while($taskClock.Elapsed.TotalSeconds -lt $seconds) {
            Start-Sleep -Seconds $interval
            $taskCurrent = Check-Process
            $taskTick = $taskClock.Elapsed.TotalSeconds
            $taskCpu = $taskCurrent.TotalProcessorTime.TotalSeconds
            $taskPercent = 100 * ($taskCpu-$taskPrevCpu) / (($taskTick-$taskPrevTick)*$taskCores)
            $taskDwmPercent = $null
            if($taskDwm) {
                $taskDwm.Refresh()
                $taskNewDwmCpu=$taskDwm.TotalProcessorTime.TotalSeconds
                $taskDwmPercent=100*($taskNewDwmCpu-$taskDwmCpu)/(($taskTick-$taskPrevTick)*$taskCores)
                $taskDwmCpu=$taskNewDwmCpu
            }
            $taskRow=[pscustomobject]@{Utc=[DateTime]::UtcNow.ToString('o');ElapsedSeconds=[Math]::Round($taskTick,3);WorkingSetMiB=$taskCurrent.WorkingSet64/1MB;PrivateMiB=$taskCurrent.PrivateMemorySize64/1MB;CpuPercent=$taskPercent;Handles=$taskCurrent.HandleCount;Threads=$taskCurrent.Threads.Count;DwmCpuPercent=$taskDwmPercent;GapSeconds=($taskTick-$taskPrevTick)}
            $taskWriter.WriteLine(($taskRow | ConvertTo-Csv -NoTypeInformation)[1]);$taskWriter.Flush()
            $taskRows.Add($taskRow);$taskPrevCpu=$taskCpu;$taskPrevTick=$taskTick
            @{Stage=$name;ElapsedSeconds=[int]$taskTick;DurationSeconds=$seconds;ProcessId=$ProcessId;UpdatedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'progress.json') -Encoding utf8
        }
    } finally { $taskWriter.Dispose() }
    $taskSummary=[ordered]@{Stage=$name;Samples=$taskRows.Count;DurationSeconds=$taskClock.Elapsed.TotalSeconds;LogicalProcessors=$taskCores;SampleIntervalSeconds=$interval;MaximumSampleGapSeconds=($taskRows.GapSeconds | Measure-Object -Maximum).Maximum}
    foreach($taskMetric in 'WorkingSetMiB','PrivateMiB','CpuPercent','Handles','Threads','DwmCpuPercent') {
        $taskValues=@($taskRows | ForEach-Object {$_.PSObject.Properties[$taskMetric].Value} | Where-Object {$null -ne $_})
        if($taskValues.Count){$taskStats=$taskValues | Measure-Object -Average -Maximum -Minimum;$taskSorted=@($taskValues | Sort-Object);$taskFirstWindow=@($taskRows | Where-Object ElapsedSeconds -le 600 | ForEach-Object {$_.PSObject.Properties[$taskMetric].Value});$taskLastWindow=@($taskRows | Where-Object ElapsedSeconds -ge ($taskClock.Elapsed.TotalSeconds-600) | ForEach-Object {$_.PSObject.Properties[$taskMetric].Value});$taskSummary[$taskMetric]=@{Mean=$taskStats.Average;Peak=$taskStats.Maximum;Minimum=$taskStats.Minimum;P95=$taskSorted[[Math]::Min($taskSorted.Count-1,[int][Math]::Floor($taskSorted.Count*0.95))];First=$taskValues[0];Last=$taskValues[-1];FirstWindowMean=($taskFirstWindow | Measure-Object -Average).Average;LastWindowMean=($taskLastWindow | Measure-Object -Average).Average}}
    }
    $taskSummary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput ($name+'-summary.json')) -Encoding utf8
}
$taskOriginal = Read-Mode
try {
    @{ProcessId=$ProcessId;ProcessStart=$taskStartTime.ToUniversalTime().ToString('o');Executable=$taskExe;SHA256=(Get-FileHash -LiteralPath $taskExe).Hash;OS=[Environment]::OSVersion.VersionString;LogicalProcessors=$taskCores;WarmupSeconds=$WarmupSeconds;SecondsPerMode=$SecondsPerMode;SoakSeconds=$SoakSeconds;SamplingProcess=$PID} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'environment.json') -Encoding utf8
    if($SoakSeconds -gt 0) {
        Measure-Stage 'soak' $SoakSeconds ([Math]::Max(10,$SampleSeconds))
    } else {
        Set-Mode $false $true
        if($WarmupSeconds -gt 0){Measure-Stage 'warmup' $WarmupSeconds 5}
        Measure-Stage 'full' $SecondsPerMode $SampleSeconds
        Set-Mode $true $true
        Measure-Stage 'compact' $SecondsPerMode $SampleSeconds
        Set-Mode $true $false
        Measure-Stage 'hidden' $SecondsPerMode $SampleSeconds
    }
    @{Status='complete';CompletedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'result.json') -Encoding utf8
} catch {
    @{Status='interrupted';Reason=$_.Exception.Message;UpdatedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'result.json') -Encoding utf8
    throw
} finally {
    $taskRemaining=Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if($taskRemaining -and $taskRemaining.StartTime -eq $taskStartTime) {if($SoakSeconds -eq 0){Set-Mode $taskOriginal.Compact $taskOriginal.Visible}}
}
