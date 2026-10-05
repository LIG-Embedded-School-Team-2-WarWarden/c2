param(
    [string]$BinDir = '.\x64\Release',
    [ValidateRange(1, 64533)]
    [int]$AssetPort = 15000
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

function Start-CmdWindow {
    param(
        [string]$Title,
        [string]$Executable,
        [string[]]$Arguments
    )

    $argumentText = $Arguments -join ' '
    $commandLine = "title $Title && `"$Executable`" $argumentText"
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $env:ComSpec
    $info.Arguments = "/k `"$commandLine`""
    $info.UseShellExecute = $true
    $info.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Normal
    return [System.Diagnostics.Process]::Start($info)
}

function Assert-UdpPortAvailable {
    param([int]$Port)

    $socket = [System.Net.Sockets.Socket]::new(
        [System.Net.Sockets.AddressFamily]::InterNetwork,
        [System.Net.Sockets.SocketType]::Dgram,
        [System.Net.Sockets.ProtocolType]::Udp)
    try {
        $socket.Bind([System.Net.IPEndPoint]::new(
            [System.Net.IPAddress]::Any, $Port))
    } catch {
        throw "UDP port $Port is already in use. Close the existing C2/dummy windows or choose another -AssetPort."
    } finally {
        $socket.Dispose()
    }
}

$serverPath = Find-Executable @('c2_server.exe', 'C2.Server.exe')
$observationPath = Find-Executable @('dummy_observation.exe', 'Dummy.Observation.exe')
$effectorPath = Find-Executable @('dummy_effector.exe', 'Dummy.Effector.exe')

$existing = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -in @('C2.Server', 'c2_server', 'Dummy.Observation',
                         'dummy_observation', 'Dummy.Effector', 'dummy_effector')
})
if ($existing.Count -gt 0) {
    $details = ($existing | ForEach-Object {
        "$($_.ProcessName) (PID $($_.Id))"
    }) -join ', '
    throw "C2/dummy processes are already running: $details. Type quit in their CMD windows before launching another set."
}

$observationStatusPort = $AssetPort + 1
$targetPort = $AssetPort + 2
$observationCommandPort = $AssetPort + 101
$effectorCommandPort = $AssetPort + 1001
$effectorStatusPort = $AssetPort + 1002

foreach ($port in @($AssetPort, $observationStatusPort, $targetPort,
                    $effectorStatusPort)) {
    Assert-UdpPortAvailable $port
}

$null = Start-CmdWindow 'C2 Server - enter commands here' $serverPath @(
    '--asset-port', "$AssetPort",
    '--observation-status-port', "$observationStatusPort",
    '--target-port', "$targetPort",
    '--observation-command-port', "$observationCommandPort",
    '--effector-command-port', "$effectorCommandPort",
    '--effector-status-port', "$effectorStatusPort"
)
Start-Sleep -Milliseconds 500

$null = Start-CmdWindow 'Observation 101' $observationPath @(
    '--asset-id', '101', '--listen-port', '0', '--c2-port', "$AssetPort",
    '--x', '0', '--y', '0'
)
$null = Start-CmdWindow 'Observation 102' $observationPath @(
    '--asset-id', '102', '--listen-port', '0', '--c2-port', "$AssetPort",
    '--x', '100', '--y', '0'
)
$null = Start-CmdWindow 'Effector 201' $effectorPath @(
    '--asset-id', '201', '--listen-port', '0', '--c2-port', "$AssetPort",
    '--x', '10', '--y', '0'
)
$null = Start-CmdWindow 'Effector 202' $effectorPath @(
    '--asset-id', '202', '--listen-port', '0', '--c2-port', "$AssetPort",
    '--x', '50', '--y', '0'
)
$null = Start-CmdWindow 'Effector 203' $effectorPath @(
    '--asset-id', '203', '--listen-port', '0', '--c2-port', "$AssetPort",
    '--x', '90', '--y', '0'
)

Write-Host 'Started one C2 server, two observation assets, and three effector assets.'
Write-Host 'Enter commands in the window titled "C2 Server - enter commands here".'
Write-Host 'Type quit in each window to stop it cleanly.'
