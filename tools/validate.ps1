$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
try {
    & (Join-Path $PSScriptRoot 'integration.ps1') -Cycles 100
    & (Join-Path $PSScriptRoot 'measure.ps1')
    & (Join-Path $PSScriptRoot 'update-report.ps1')
    New-Item -ItemType Directory -Path (Join-Path $taskRoot 'artifacts\soak') -Force | Out-Null
    @{Stage='soak';ElapsedSeconds=0;DurationSeconds=86400} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskRoot 'artifacts\soak\progress.json') -Encoding utf8
    & (Join-Path $PSScriptRoot 'update-report.ps1')
    & (Join-Path $PSScriptRoot 'measure.ps1') -SoakSeconds 86400 -SampleSeconds 60 -OutputDirectory (Join-Path $taskRoot 'artifacts\soak')
    & (Join-Path $PSScriptRoot 'update-report.ps1')
    @{Status='complete';UpdatedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskRoot 'artifacts\validation-result.json') -Encoding utf8
} catch {
    @{Status='interrupted';Reason=$_.Exception.Message;UpdatedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskRoot 'artifacts\validation-result.json') -Encoding utf8
    & (Join-Path $PSScriptRoot 'update-report.ps1')
    throw
}
