param([string]$Executable = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\file_analyzer.exe'))
$ErrorActionPreference = 'Stop'
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('file-analyzer-cli-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$fixtureName = 'small ' + [char]0x03B4 + ' & file.csv'
$inputPath = Join-Path $testDirectory $fixtureName
$csvPath = Join-Path $testDirectory 'quoted comparison.csv'
$emptyPath = Join-Path $testDirectory 'empty.txt'
$checks = 0
function Invoke-AnalyzerCheck {
    param([string[]]$Arguments, [int]$ExpectedExit = 0)
    $lines = & $Executable @Arguments 2>&1
    if ($LASTEXITCODE -ne $ExpectedExit) { throw "Unexpected exit $LASTEXITCODE for $Arguments : $lines" }
    $script:checks++
    return ($lines -join "`n")
}
try {
    [System.IO.File]::WriteAllText($inputPath, "ERROR WARN INFO`r`nerror ERRORS`nWARN", [System.Text.UTF8Encoding]::new($false))
    $originalHash = (Get-FileHash -LiteralPath $inputPath).Hash
    Invoke-AnalyzerCheck -Arguments @('--help') | Out-Null
    foreach ($mode in 'buffered','mapped','mapped-parallel') {
        $arguments = @('analyze', $inputPath, '--mode', $mode)
        if ($mode -eq 'mapped-parallel') { $arguments += @('--threads','4') }
        $text = Invoke-AnalyzerCheck -Arguments $arguments
        if ($text -notmatch 'Lines: 3' -or $text -notmatch 'Words: 6' -or $text -notmatch 'ERROR: 1' -or $text -notmatch 'WARN: 2' -or $text -notmatch 'INFO: 1') {
            throw "Unexpected analysis: $text"
        }
        $checks++
    }
    Invoke-AnalyzerCheck -Arguments @('benchmark', $inputPath, '--threads','1,4','--runs','2','--output',$csvPath) | Out-Null
    $trials = @(Import-Csv -LiteralPath $csvPath)
    if ($trials.Count -ne 8 -or @($trials | Where-Object { $_.results_match -ne 'true' -or $_.lines -ne '3' -or $_.words -ne '6' }).Count -ne 0) {
        throw 'Invalid subprocess benchmark CSV'
    }
    $summaries = @(Import-Csv -LiteralPath (Join-Path $testDirectory 'quoted comparison.summary.csv'))
    if ($summaries.Count -ne 4) { throw 'Invalid summary CSV' }
    $checks++
    Invoke-AnalyzerCheck -Arguments @('analyze',$inputPath,'--threads','0') -ExpectedExit 1 | Out-Null
    Invoke-AnalyzerCheck -Arguments @('analyze',$inputPath,'--threads','257') -ExpectedExit 1 | Out-Null
    Invoke-AnalyzerCheck -Arguments @('analyze',$inputPath,'--mode','invalid') -ExpectedExit 1 | Out-Null
    Invoke-AnalyzerCheck -Arguments @('analyze',$inputPath,'--threads','1','--threads','2') -ExpectedExit 1 | Out-Null
    Invoke-AnalyzerCheck -Arguments @('analyze',$inputPath,'--unknown','1') -ExpectedExit 1 | Out-Null
    Invoke-AnalyzerCheck -Arguments @('analyze',$inputPath,'--mode','buffered','--threads','2') -ExpectedExit 1 | Out-Null
    Invoke-AnalyzerCheck -Arguments @('benchmark',$inputPath,'--runs','0') -ExpectedExit 1 | Out-Null
    Invoke-AnalyzerCheck -Arguments @('benchmark',$inputPath,'--threads','1,') -ExpectedExit 1 | Out-Null
    Invoke-AnalyzerCheck -Arguments @('benchmark',$inputPath,'--output',$inputPath) -ExpectedExit 1 | Out-Null
    Invoke-AnalyzerCheck -Arguments @('generate','--output',$inputPath,'--size-bytes','1') -ExpectedExit 1 | Out-Null
    Invoke-AnalyzerCheck -Arguments @('generate','--output',$emptyPath,'--size-bytes','0','--seed','0') | Out-Null
    Invoke-AnalyzerCheck -Arguments @('benchmark',$emptyPath,'--threads','1,4','--runs','1','--output',(Join-Path $testDirectory 'empty.csv')) | Out-Null
    if ((Get-FileHash -LiteralPath $inputPath).Hash -ne $originalHash) { throw 'Input was modified by tests' }
    $checks++
    Write-Output "Passed $checks CLI and subprocess checks."
}
finally {
    # This directory was created exclusively for this run; remove only known files.
    foreach ($name in @($fixtureName,'empty.txt','quoted comparison.csv','quoted comparison.summary.csv','quoted comparison.md','quoted comparison.metadata.txt','empty.csv','empty.summary.csv','empty.md','empty.metadata.txt')) {
        $testPath = Join-Path $testDirectory $name
        if (Test-Path -LiteralPath $testPath) { Remove-Item -LiteralPath $testPath }
    }
    Remove-Item -LiteralPath $testDirectory
}
