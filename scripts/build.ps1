param(
    [switch]$RunTests,
    [string]$Compiler = 'g++'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
$compilerCommand = Get-Command $Compiler -ErrorAction Stop
$commonArguments = @('-std=c++20', '-O3', '-DNDEBUG', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-Wall', '-Wextra', '-Wpedantic', '-pthread', '-I', (Join-Path $projectRoot 'include'))
$coreSources = @('scanner.cpp', 'mapped_file_windows.cpp', 'analyzer.cpp', 'metrics_windows.cpp', 'dataset_generator.cpp') | ForEach-Object { Join-Path $projectRoot "src/$_" }
$appSources = @('main.cpp', 'benchmark.cpp', 'report.cpp') | ForEach-Object { Join-Path $projectRoot "src/$_" }
$executable = Join-Path $buildDirectory 'file_analyzer.exe'
& $compilerCommand.Source @commonArguments @coreSources @appSources '-o' $executable '-lpsapi' '-lshell32' '-ladvapi32' '-static'
if ($LASTEXITCODE -ne 0) { throw "Application build failed with exit code $LASTEXITCODE" }
Write-Output "Built $executable"
if ($RunTests) {
    $testExecutable = Join-Path $buildDirectory 'analyzer_tests.exe'
    & $compilerCommand.Source @commonArguments @coreSources (Join-Path $projectRoot 'tests/correctness_tests.cpp') '-o' $testExecutable '-lpsapi' '-static'
    if ($LASTEXITCODE -ne 0) { throw "Test build failed with exit code $LASTEXITCODE" }
    & $testExecutable
    if ($LASTEXITCODE -ne 0) { throw "Correctness tests failed with exit code $LASTEXITCODE" }
    & (Join-Path $PSScriptRoot 'smoke-tests.ps1') -Executable $executable
}
