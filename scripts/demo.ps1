param(
    [ValidateRange(1,1024)][int]$SizeMiB = 64,
    [ValidateRange(1,100)][int]$Runs = 7,
    [string]$Threads = '1,2,4,8,12'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot 'build\file_analyzer.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    & (Join-Path $PSScriptRoot 'build.ps1') -RunTests
}
$dataDirectory = Join-Path $env:LOCALAPPDATA 'FileAnalyzer\OS_project_2026\datasets'
$inputPath = Join-Path $dataDirectory "sample-${SizeMiB}mib-seed42.log"
if (-not (Test-Path -LiteralPath $inputPath)) {
    & $executable generate --output $inputPath --size-mib $SizeMiB --seed 42
    if ($LASTEXITCODE -ne 0) { throw 'Dataset generation failed' }
}
$outputPath = Join-Path $projectRoot "results\benchmark-${SizeMiB}mib.csv"
& $executable benchmark $inputPath --threads $Threads --runs $Runs --output $outputPath
if ($LASTEXITCODE -ne 0) { throw 'Benchmark failed' }
