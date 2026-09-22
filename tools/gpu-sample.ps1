$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskApp=Get-Process DeskPerch
$taskDwm=Get-Process dwm | Where-Object SessionId -eq $taskApp.SessionId | Select-Object -First 1
$taskRows=[Collections.Generic.List[object]]::new()
$taskOutput=Join-Path $taskRoot 'artifacts\gpu.json'
try {
    while(Get-Process -Id $taskApp.Id -ErrorAction SilentlyContinue) {
        $taskProgressPath=Join-Path $taskRoot 'artifacts\performance\progress.json'
        try{$taskStage=(Get-Content -LiteralPath $taskProgressPath -Raw | ConvertFrom-Json).Stage}catch{$taskStage='unknown'}
        $taskSample=Get-Counter -Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction Stop
        $taskOwn=@($taskSample.CounterSamples | Where-Object {$_.InstanceName -match ('^pid_'+$taskApp.Id+'_') -and $_.Status -eq 0})
        $taskDesktop=@($taskSample.CounterSamples | Where-Object {$_.InstanceName -match ('^pid_'+$taskDwm.Id+'_') -and $_.Status -eq 0})
        $taskRows.Add([pscustomobject]@{Utc=[DateTime]::UtcNow.ToString('o');Stage=$taskStage;AppEngineInstances=$taskOwn.Count;AppBusiestEnginePercent=if($taskOwn.Count){($taskOwn.CookedValue | Measure-Object -Maximum).Maximum}else{$null};DwmBusiestEnginePercent=if($taskDesktop.Count){($taskDesktop.CookedValue | Measure-Object -Maximum).Maximum}else{$null}})
        $taskResultPath=Join-Path $taskRoot 'artifacts\performance\result.json'
        if(Test-Path -LiteralPath $taskResultPath){$taskResult=Get-Content -LiteralPath $taskResultPath -Raw | ConvertFrom-Json;if($taskResult.Status -in 'complete','interrupted'){break}}
        Start-Sleep -Seconds 10
    }
    $taskSummary=[ordered]@{Status='complete';Metric='Busiest reported GPU engine per process, not sum of engines';Attribution='Other apps and Wallpaper Engine running; observed DWM differences are not causal attribution';Samples=$taskRows.Count;Stages=@()}
    foreach($taskName in 'full','compact','hidden'){$taskStageRows=@($taskRows | Where-Object Stage -eq $taskName);if($taskStageRows.Count){$taskSummary.Stages+=@{Stage=$taskName;Samples=$taskStageRows.Count;AppEngineInstancesPeak=($taskStageRows.AppEngineInstances | Measure-Object -Maximum).Maximum;AppGPU=($taskStageRows.AppBusiestEnginePercent | Measure-Object -Average -Maximum);DwmGPU=($taskStageRows.DwmBusiestEnginePercent | Measure-Object -Average -Maximum)}}}
    $taskSummary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $taskOutput -Encoding utf8
    $taskRows | Export-Csv -LiteralPath (Join-Path $taskRoot 'artifacts\gpu.csv') -NoTypeInformation -Encoding utf8
} catch { @{Status='unavailable';Reason=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath $taskOutput -Encoding utf8 }
& (Join-Path $PSScriptRoot 'update-report.ps1')
