param(
    [string]$BinDir = '.\build\Release',
    [ValidateRange(1000, 1000000)][int]$Samples = 50000,
    [ValidateRange(1, 10)][int]$Runs = 3,
    [string]$OutputPath = '.\build\runtime-benchmark.json'
)
$ErrorActionPreference = 'Stop'
$executable = Join-Path $BinDir 'c2_benchmark.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw "Build with -DC2_BUILD_BENCHMARKS=ON first." }
$results = @()
for ($runIndex = 0; $runIndex -lt $Runs; $runIndex++) {
    $raw = & $executable $Samples
    if ($LASTEXITCODE -ne 0) { throw "Benchmark failed; output is not a valid performance result: $raw" }
    $result = $raw | ConvertFrom-Json
    if ($result.rejected -ne 0 -or $result.active_tracks -ne 512) { throw 'Workload invariants failed.' }
    $results += $result
}
$report = [ordered]@{
    schema_version = 1
    captured_at_utc = [DateTime]::UtcNow.ToString('o')
    git_commit = (git rev-parse HEAD)
    working_tree_dirty = [bool](git status --porcelain)
    environment = [ordered]@{
        os = [Environment]::OSVersion.VersionString
        processor = $env:PROCESSOR_IDENTIFIER
        logical_processors = [Environment]::ProcessorCount
        configuration = 'Release'
    }
    scope = 'In-process encode + authenticated Runtime ingest; excludes UDP, queue wait, physical assets and output control.'
    runs = $results
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Output "Runtime benchmark written to $OutputPath"
$results | Format-Table -Property @('samples', 'active_tracks', 'rejected', 'messages_per_second', 'p50_us', 'p95_us', 'p99_us')
