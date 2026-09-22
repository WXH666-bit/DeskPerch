param([ValidateSet('Release','Debug')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
    if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
    cmake --build build --config $Configuration --parallel
    if($LASTEXITCODE -ne 0){throw 'Build failed'}
    ctest --test-dir build -C $Configuration --output-on-failure
    if($LASTEXITCODE -ne 0){throw 'Tests failed'}
    New-Item -ItemType Directory -Path out -Force | Out-Null
    Copy-Item -LiteralPath "build\$Configuration\DeskPerch.exe" -Destination out\DeskPerch.exe
} finally { Pop-Location }
