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
    param([string]$Path)
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Path
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    return [System.Diagnostics.Process]::Start($info)
}

$serverPath = Find-Executable @('c2_server.exe', 'C2.Server.exe')
$observationPath = Find-Executable @('dummy_observation.exe', 'Dummy.Observation.exe')
$effectorPath = Find-Executable @('dummy_effector.exe', 'Dummy.Effector.exe')
$processes = @()

try {
    $server = Start-RedirectedProcess $serverPath
    $observation = Start-RedirectedProcess $observationPath
    $effector = Start-RedirectedProcess $effectorPath
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
    Write-Host 'UDP process smoke test passed: six commands, connected assets, no pending ACK.'
} finally {
    foreach ($process in $processes) {
        if ($null -ne $process -and -not $process.HasExited) {
            $process.Kill($true)
            $process.WaitForExit()
        }
        if ($null -ne $process) { $process.Dispose() }
    }
}
