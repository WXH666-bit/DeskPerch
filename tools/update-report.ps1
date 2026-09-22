$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskMutex=[Threading.Mutex]::new($false,'Local\DeskPerch.ReportWriter')
if(-not $taskMutex.WaitOne(10000)){throw 'Report writer is busy'}
try {
function Read-Json([string]$relative) {
    $taskPath=Join-Path $taskRoot $relative
    if(Test-Path -LiteralPath $taskPath){return Get-Content -LiteralPath $taskPath -Raw -Encoding utf8 | ConvertFrom-Json}
    return $null
}
$taskLines=[Collections.Generic.List[string]]::new()
$taskLines.Add('# 资源实测报告')
$taskLines.Add('')
$taskLines.Add(('更新时间：{0}。数据来自 Release EXE 的实际进程采样。' -f [DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz')))
$taskLines.Add('')
$taskEnvironment=Read-Json 'artifacts/performance/environment.json'
if($taskEnvironment){$taskLines.Add(('系统：{0}；逻辑处理器：{1}；EXE SHA-256：`{2}`。' -f $taskEnvironment.OS,$taskEnvironment.LogicalProcessors,$taskEnvironment.SHA256));$taskLines.Add('')}
$taskLines.Add('## 闲置场景')
$taskLines.Add('')
$taskLines.Add('预热 300 秒，三个场景各采样 600 秒，间隔 1 秒。CPU 为整机归一化占比：进程 CPU 时间增量 / 墙钟时间增量 / 逻辑处理器数 × 100%。MiB 为 1024² 字节；未强制清空或修剪工作集。采样工具是独立开发工具，不计入应用进程数据。')
$taskLines.Add('')
$taskLines.Add('| 场景 | 工作集均值 / 峰值 MiB | 私有提交均值 / 峰值 MiB | CPU 均值 / 峰值 | 句柄起止 | 线程起止 |')
$taskLines.Add('| --- | ---: | ---: | ---: | ---: | ---: |')
foreach($taskStage in 'full','compact','hidden') {
    $taskName=@{full='完整';compact='简约';hidden='隐藏'}[$taskStage]
    $taskData=Read-Json ('artifacts/performance/'+$taskStage+'-summary.json')
    if($taskData){$taskLines.Add(('| {0} | {1:F2} / {2:F2} | {3:F2} / {4:F2} | {5:F4}% / {6:F4}% | {7} / {8} | {9} / {10} |' -f $taskName,$taskData.WorkingSetMiB.Mean,$taskData.WorkingSetMiB.Peak,$taskData.PrivateMiB.Mean,$taskData.PrivateMiB.Peak,$taskData.CpuPercent.Mean,$taskData.CpuPercent.Peak,$taskData.Handles.First,$taskData.Handles.Last,$taskData.Threads.First,$taskData.Threads.Last))}
    else{$taskLines.Add("| $taskName | 尚未完成 | — | — | — | — |")}
}
$taskLines.Add('')
$taskLines.Add('目标：工作集 ≤30 MiB、私有提交 ≤20 MiB，10 分钟平均 CPU ≤0.1%。峰值为采样点峰值，不能代表两次采样之间的瞬时峰值。')
$taskBaseResult=Read-Json 'artifacts/performance/result.json'
if($taskBaseResult -and $taskBaseResult.Status -eq 'interrupted'){$taskLines.Add(('采样中断：{0}。未完成的场景不记为通过。' -f $taskBaseResult.Reason))}
$taskLines.Add('')
$taskLines.Add('## 典型操作压力')
$taskLines.Add('')
$taskStress=Read-Json 'artifacts/integration/summary.json'
if($taskStress){$taskLines.Add(('{0} 次模式、显示/隐藏、锁定切换，用时 {1:F2} 秒；平均 CPU {2:F3}%。工作集采样峰值 {3:F2} MiB、私有提交采样峰值 {4:F2} MiB。句柄 {5} → {6}，线程 {7} → {8}。' -f $taskStress.Operations,$taskStress.DurationSeconds,$taskStress.CpuPercentMean,$taskStress.PeakWorkingSetMiB,$taskStress.PeakPrivateMiB,$taskStress.First.Handles,$taskStress.Last.Handles,$taskStress.First.Threads,$taskStress.Last.Threads))}
$taskLines.Add('')
$taskLines.Add('## 24 小时稳定性')
$taskLines.Add('')
$taskSoak=Read-Json 'artifacts/soak/soak-summary.json'
$taskSoakResult=Read-Json 'artifacts/soak/result.json'
$taskSoakProgress=Read-Json 'artifacts/soak/progress.json'
if($taskSoakResult -and $taskSoakResult.Status -eq 'complete' -and $taskSoak){
    $taskLines.Add(('已采样 {0:F2} 小时，共 {1} 个采样点，最大采样间隔 {2:F2} 秒。' -f ($taskSoak.DurationSeconds/3600),$taskSoak.Samples,$taskSoak.MaximumSampleGapSeconds))
    $taskLines.Add(('私有提交首尾 10 分钟均值 {0:F2} → {1:F2} MiB；工作集峰值 {2:F2} MiB；CPU 均值 {3:F4}%；句柄 {4} → {5}。' -f $taskSoak.PrivateMiB.FirstWindowMean,$taskSoak.PrivateMiB.LastWindowMean,$taskSoak.WorkingSetMiB.Peak,$taskSoak.CpuPercent.Mean,$taskSoak.Handles.First,$taskSoak.Handles.Last))
    if($taskSoak.MaximumSampleGapSeconds -gt 120){$taskLines.Add('存在较长采样空档，可能涉及系统休眠；不能视为连续 24 小时活动状态验证。')}
    elseif(($taskSoak.PrivateMiB.LastWindowMean-$taskSoak.PrivateMiB.FirstWindowMean) -le 2 -and $taskSoak.Handles.Last -le $taskSoak.Handles.First){$taskLines.Add('本次采样未发现超过 2 MiB 的首尾私有提交增长或句柄累积。此结论仅覆盖记录中的设备与使用条件。')}
    else{$taskLines.Add('首尾资源指标存在增长，需要进一步检查；本报告不将其自动判为通过。')}
} elseif($taskSoakResult -and $taskSoakResult.Status -eq 'interrupted'){$taskLines.Add(('已中断：{0}。没有完整 24 小时实测结果。' -f $taskSoakResult.Reason))}
elseif($taskSoakProgress){$taskLines.Add('采样进行中，完成或中断后本文件会自动更新。当前原始进度位于 `artifacts/soak/progress.json`。')}
else{$taskLines.Add('等待三种模式采样结束后开始；没有把短时测试外推为 24 小时结果。')}
$taskLines.Add('')
$taskLines.Add('## 范围与原始记录')
$taskLines.Add('')
$taskLines.Add('- `artifacts/performance`：三模式 CSV、摘要、环境、二进制摘要和进度。')
$taskLines.Add('- `artifacts/integration`：操作压力 CSV、窗口状态及重启验证。')
$taskLines.Add('- `artifacts/soak`：24 小时采样，进程退出或重启将中断该序列。')
$taskLines.Add('- DWM CPU 同步记录在 CSV 中。其他应用和 Wallpaper Engine 同时运行，因此 DWM 的变化不能单独归因于本应用；尚无隔离 GPU 增量实验。')
$taskGPU=Read-Json 'artifacts/gpu.json'
if($taskGPU -and $taskGPU.Status -eq 'unavailable') {
    $taskLines.Add(('GPU 计数器采样不可用：{0}。没有可用于验收的 GPU 增量结果。' -f $taskGPU.Reason))
}
if($taskGPU -and $taskGPU.Status -eq 'complete') {
    $taskLines.Add('')
    $taskLines.Add('GPU 计数器观察（每进程最忙的引擎，不对不同引擎求和）：')
    $taskLines.Add('')
    $taskLines.Add('| 场景 | 样本数 | 应用 GPU 引擎实例峰值 | DWM 最忙引擎均值 / 峰值 |')
    $taskLines.Add('| --- | ---: | ---: | ---: |')
    foreach($taskGPUStage in $taskGPU.Stages){$taskLines.Add(('| {0} | {1} | {2} | {3:F3}% / {4:F3}% |' -f $taskGPUStage.Stage,$taskGPUStage.Samples,$taskGPUStage.AppEngineInstancesPeak,$taskGPUStage.DwmGPU.Average,$taskGPUStage.DwmGPU.Maximum))}
    $taskLines.Add('应用未出现独立 GPU 引擎实例时，仅表示计数器未报告该进程的独立 GPU 工作；不能据此认定 DWM 合成代价为零。')
}
$taskLines.Add('- 未进行物理插拔、厂商 DPI 切换、真实锁屏/唤醒等动作的项目，见 `docs/ACCEPTANCE.md`，不以模拟或代码检查冒充实机通过。')
$taskLines | Set-Content -LiteralPath (Join-Path $taskRoot 'docs\PERFORMANCE.md') -Encoding utf8
if($taskBaseResult -and $taskBaseResult.Status -eq 'complete') {
    # Correct the header of an early sampler revision; all nine recorded values are retained.
    foreach($taskCSV in (Get-ChildItem -LiteralPath (Join-Path $taskRoot 'artifacts\performance') -Filter '*.csv')) {
        $taskText=[IO.File]::ReadAllText($taskCSV.FullName)
        if($taskText.StartsWith('Utc,ElapsedSeconds,WorkingSetMiB,PrivateMiB,CpuPercent,Handles,Threads,DwmCpuPercent'+"`r`n")) {
            $taskText=$taskText.Replace('Utc,ElapsedSeconds,WorkingSetMiB,PrivateMiB,CpuPercent,Handles,Threads,DwmCpuPercent'+"`r`n",'Utc,ElapsedSeconds,WorkingSetMiB,PrivateMiB,CpuPercent,Handles,Threads,DwmCpuPercent,GapSeconds'+"`r`n")
            [IO.File]::WriteAllText($taskCSV.FullName,$taskText,[Text.UTF8Encoding]::new($false))
        }
    }
}
if(Test-Path -LiteralPath (Join-Path $taskRoot 'out\docs')){Copy-Item -LiteralPath (Join-Path $taskRoot 'docs\PERFORMANCE.md') -Destination (Join-Path $taskRoot 'out\docs\PERFORMANCE.md')}
} finally {$taskMutex.ReleaseMutex();$taskMutex.Dispose()}
