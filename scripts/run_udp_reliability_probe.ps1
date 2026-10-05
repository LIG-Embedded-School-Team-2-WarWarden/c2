param(
    [string]$BinDir = './build/Release',
    [string]$OutputPath = './build/udp-reliability.json'
)
$ErrorActionPreference = 'Stop'
$executable = Join-Path $BinDir 'c2_udp_probe.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build with -DC2_BUILD_BENCHMARKS=ON first.' }
$results = @()
foreach ($scenario in @('burst', 'faults')) {
    $raw = & $executable $scenario
    if ($LASTEXITCODE -ne 0) { throw "UDP probe invariants failed: $raw" }
    $results += ($raw | ConvertFrom-Json)
}
git diff --quiet HEAD
$trackedDirty = $LASTEXITCODE -ne 0
$report = [ordered]@{
    schema_version = 1
    captured_at_utc = [DateTime]::UtcNow.ToString('o')
    git_commit = (git rev-parse HEAD)
    tracked_working_tree_dirty = $trackedDirty
    untracked_files_count = @(git ls-files --others --exclude-standard).Count
    environment = [ordered]@{
        os = [Environment]::OSVersion.VersionString
        processor = $env:PROCESSOR_IDENTIFIER
        logical_processors = [Environment]::ProcessorCount
        configuration = 'Release'
    }
    scope = 'Real loopback UDP relays + bounded server ingress + command ACK tracker; scripted idempotent STOP simulator, no physical device. Logging disabled.'
    runs = $results
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
$results | Format-Table scenario, commands, completed, queue_dropped_full, ack_p95_us
