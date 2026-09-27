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
        '--observation-status-port', '15001',
        '--target-port', '15002',
        '--observation-command-port', '15101',
        '--effector-command-port', '16001',
        '--effector-status-port', '16002',
        '--target-validity-ms', '3000',
        '--heartbeat-timeout-ms', '4000',
        '--command-validity-ms', '700',
        '--ack-timeout-ms', '300',
        '--command-attempts', '4',
        '--emergency-stop-repetitions', '4'
    )
    $observation = Start-RedirectedProcess $observationPath @(
        '--listen-port', '15101',
        '--status-port', '15001',
        '--target-port', '15002',
        '--watchdog-timeout-ms', '4000'
    )
    $effector = Start-RedirectedProcess $effectorPath @(
        '--listen-port', '16001',
        '--status-port', '16002',
        '--watchdog-timeout-ms', '4000'
    )
    $processes = @($server, $observation, $effector)

    Start-Sleep -Seconds 2
    foreach ($entry in @(
        @{ Command = 'scan 10 5'; Delay = 80 },
        @{ Command = 'obs-stop'; Delay = 80 },
        @{ Command = 'obs-home'; Delay = 80 },
        @{ Command = 'point 1'; Delay = 80 },
        @{ Command = 'arm 1'; Delay = 80 },
        @{ Command = 'start 1 100'; Delay = 150 }
    )) {
        $server.StandardInput.WriteLine($entry.Command)
        Start-Sleep -Milliseconds $entry.Delay
    }
    $server.StandardInput.WriteLine('status')
    $server.StandardInput.WriteLine('quit')
    $observation.StandardInput.WriteLine('quit')
    $effector.StandardInput.WriteLine('quit')

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
    if ($serverOutput -notmatch 'observation=CONNECTED effector=CONNECTED pending_commands=0') {
        throw "Assets did not finish connected with no pending command.`n${serverOutput}`n${allErrors}"
    }
    foreach ($process in $processes) {
        if ($process.ExitCode -ne 0) {
            throw "Process failed with exit code $($process.ExitCode): $($process.StartInfo.FileName)"
        }
    }
    Assert-InvalidConfiguration $serverPath @('--command-attempts', '0') 'invalid command attempts'
    Assert-InvalidConfiguration $observationPath @('--listen-port', '0') 'invalid UDP port'
    Assert-InvalidConfiguration $effectorPath @('--watchdog-timeout-ms', '0') 'invalid watchdog timeout'
    Write-Host 'UDP process smoke test passed: runtime ports and timing, six commands, connected assets, no pending ACK, invalid options rejected.'
} finally {
    foreach ($process in $processes) {
        if ($null -ne $process -and -not $process.HasExited) {
            $process.Kill($true)
            $process.WaitForExit()
        }
        if ($null -ne $process) { $process.Dispose() }
    }
}
