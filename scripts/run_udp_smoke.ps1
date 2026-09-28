param(
    [Parameter(Mandatory = $true)]
    [string]$BinDir
)

$ErrorActionPreference = 'Stop'

function Find-Executable {
    param([string[]]$Names)
    foreach ($name in $Names) {
        $candidate = Join-Path $BinDir $name
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Executable not found in ${BinDir}: $($Names -join ', ')"
}

function Start-RedirectedProcess {
    param(
        [string]$Path,
        [string[]]$ArgumentList = @()
    )
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Path
    foreach ($argument in $ArgumentList) {
        $null = $info.ArgumentList.Add($argument)
    }
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    return [System.Diagnostics.Process]::Start($info)
}

function Assert-InvalidConfiguration {
    param(
        [string]$Path,
        [string[]]$ArgumentList,
        [string]$ExpectedError
    )
    $process = Start-RedirectedProcess $Path $ArgumentList
    try {
        if (-not $process.WaitForExit(5000)) {
            throw "Invalid-configuration process did not exit: $Path"
        }
        $errorOutput = $process.StandardError.ReadToEnd()
        if ($process.ExitCode -eq 0 -or $errorOutput -notmatch [regex]::Escape($ExpectedError)) {
            throw "Expected configuration error '${ExpectedError}'. Exit=$($process.ExitCode), stderr=${errorOutput}"
        }
    } finally {
        if (-not $process.HasExited) { $process.Kill($true) }
        $process.Dispose()
    }
}

$serverPath = Find-Executable @('c2_server.exe', 'C2.Server.exe')
$observationPath = Find-Executable @('dummy_observation.exe', 'Dummy.Observation.exe')
$effectorPath = Find-Executable @('dummy_effector.exe', 'Dummy.Effector.exe')
$processes = @()

try {
    $server = Start-RedirectedProcess $serverPath @(
        '--asset-port', '15000',
        '--observation-status-port', '15001',
        '--target-port', '15002',
        '--observation-command-port', '15101',
        '--effector-command-port', '16001',
        '--effector-status-port', '16002',
        '--target-validity-ms', '3000',
        '--heartbeat-interval-ms', '700',
        '--heartbeat-timeout-ms', '4000',
        '--command-validity-ms', '700',
        '--ack-timeout-ms', '300',
        '--command-attempts', '4',
        '--emergency-stop-repetitions', '4'
    )
    $observation1 = Start-RedirectedProcess $observationPath @(
        '--asset-id', '101', '--listen-port', '0', '--c2-port', '15000',
        '--status-interval-ms', '80',
        '--heartbeat-interval-ms', '300', '--registration-interval-ms', '500',
        '--target-interval-ms', '300',
        '--watchdog-timeout-ms', '4000'
    )
    $observation2 = Start-RedirectedProcess $observationPath @(
        '--asset-id', '102', '--listen-port', '0', '--c2-port', '15000',
        '--status-interval-ms', '80',
        '--heartbeat-interval-ms', '300', '--registration-interval-ms', '500',
        '--target-interval-ms', '300', '--y', '20',
        '--watchdog-timeout-ms', '4000'
    )
    $effector1 = Start-RedirectedProcess $effectorPath @(
        '--asset-id', '201', '--listen-port', '0', '--c2-port', '15000',
        '--status-interval-ms', '80',
        '--heartbeat-interval-ms', '300', '--registration-interval-ms', '500',
        '--watchdog-timeout-ms', '4000'
    )
    $effector2 = Start-RedirectedProcess $effectorPath @(
        '--asset-id', '202', '--listen-port', '0', '--c2-port', '15000',
        '--status-interval-ms', '80',
        '--heartbeat-interval-ms', '300', '--registration-interval-ms', '500',
        '--watchdog-timeout-ms', '4000', '--x', '50'
    )
    $effector3 = Start-RedirectedProcess $effectorPath @(
        '--asset-id', '203', '--listen-port', '0', '--c2-port', '15000',
        '--status-interval-ms', '80',
        '--heartbeat-interval-ms', '300', '--registration-interval-ms', '500',
        '--watchdog-timeout-ms', '4000', '--x', '90'
    )
    $assets = @($observation1, $observation2, $effector1, $effector2, $effector3)
    $processes = @($server) + $assets

    Start-Sleep -Seconds 2
    foreach ($entry in @(
        @{ Command = 'scan 101 10 5'; Delay = 80 },
        @{ Command = 'obs-stop 101'; Delay = 80 },
        @{ Command = 'obs-home 101'; Delay = 80 },
        @{ Command = 'point 1'; Delay = 80 },
        @{ Command = 'arm 1'; Delay = 80 },
        @{ Command = 'start 1 100'; Delay = 150 }
    )) {
        $server.StandardInput.WriteLine($entry.Command)
        Start-Sleep -Milliseconds $entry.Delay
    }
    $server.StandardInput.WriteLine('assets')
    Start-Sleep -Milliseconds 100
    $effector1.StandardInput.WriteLine('quit')
    if (-not $effector1.WaitForExit(5000)) {
        throw 'Original effector 201 did not exit for session replacement'
    }
    $effector1Restart = Start-RedirectedProcess $effectorPath @(
        '--asset-id', '201', '--listen-port', '0', '--c2-port', '15000',
        '--status-interval-ms', '80',
        '--heartbeat-interval-ms', '300', '--registration-interval-ms', '500',
        '--watchdog-timeout-ms', '4000'
    )
    $assets += $effector1Restart
    $processes += $effector1Restart
    Start-Sleep -Seconds 1
    $server.StandardInput.WriteLine('assets')
    $server.StandardInput.WriteLine('status')
    $server.StandardInput.WriteLine('estop-all')
    $server.StandardInput.WriteLine('quit')
    foreach ($asset in $assets) {
        if (-not $asset.HasExited) { $asset.StandardInput.WriteLine('quit') }
    }

    foreach ($process in $processes) {
        if (-not $process.WaitForExit(5000)) {
            throw "Process did not exit within five seconds: $($process.StartInfo.FileName)"
        }
    }

    $serverOutput = $server.StandardOutput.ReadToEnd()
    $allErrors = ($processes | ForEach-Object { $_.StandardError.ReadToEnd() }) -join "`n"
    $sentCount = ([regex]::Matches($serverOutput, '> sent')).Count
    if ($sentCount -ne 6) {
        throw "Expected six successful commands but observed ${sentCount}.`n${serverOutput}`n${allErrors}"
    }
    if ($serverOutput -notmatch 'assets=5 tracks=2 pending_commands=0' -or
        $serverOutput -notmatch 'estop assets=3 datagrams=12') {
        throw "Dynamic assets or emergency stop summary was incorrect.`n${serverOutput}`n${allErrors}"
    }
    $sessions = [regex]::Matches(
        $serverOutput, 'asset=201 role=2 session=(\d+)') |
        ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique
    if ($sessions.Count -lt 2) {
        throw "Effector 201 did not register a new session after restart.`n${serverOutput}`n${allErrors}"
    }
    foreach ($process in $processes) {
        if ($process.ExitCode -ne 0) {
            throw "Process failed with exit code $($process.ExitCode): $($process.StartInfo.FileName)"
        }
    }
    Assert-InvalidConfiguration $serverPath @('--heartbeat-interval-ms', '0') 'invalid heartbeat interval'
    Assert-InvalidConfiguration $observationPath @('--target-interval-ms', '0') 'invalid target interval'
    Assert-InvalidConfiguration $effectorPath @('--status-interval-ms', '0') 'invalid status interval'
    Write-Host 'UDP process smoke test passed: 2 observations, 3 effectors, session replacement, 2 tracks, automatic assignment, attack flow, estop-all.'
} finally {
    foreach ($process in $processes) {
        if ($null -ne $process -and -not $process.HasExited) {
            $process.Kill($true)
            $process.WaitForExit()
        }
        if ($null -ne $process) { $process.Dispose() }
    }
}
