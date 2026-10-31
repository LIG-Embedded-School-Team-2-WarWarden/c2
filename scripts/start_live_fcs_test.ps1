param(
    [string]$EcsAddress = '10.0.0.105',
    [string]$FcsAddress = '10.0.0.3',
    [ValidateRange(1, 65535)][int]$FcsPort = 2000,
    [int]$ReuseFakeRdrProcessId = 0
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$backendExe = Join-Path $workspace 'build/fcs-live-original-gui/Release/c2_fcs_gui.exe'
$guiExe = Join-Path $workspace 'build/gui-upstream-bin/AadcCommandCenter.exe'
$pythonWindow = Join-Path (Split-Path (Get-Command python.exe).Source -Parent) 'pythonw.exe'
foreach ($executable in @($backendExe, $guiExe, $pythonWindow)) {
    if (-not (Test-Path -LiteralPath $executable)) { throw "Executable missing: $executable" }
}
$sessionFolder = Join-Path $workspace ('build/live-fcs-sessions/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $sessionFolder -Force | Out-Null
$stopFile = Join-Path $sessionFolder 'stop'
$statusFile = Join-Path $sessionFolder 'status.json'
$backendProcess = $rdrProcess = $guiProcess = $null
$createdFakeRdr = $false
try {
    if ($ReuseFakeRdrProcessId -gt 0) {
        $rdrProcess = Get-Process -Id $ReuseFakeRdrProcessId -ErrorAction Stop
        if ($rdrProcess.ProcessName -notin @('python', 'pythonw')) { throw 'Expected the existing fake RDR Python process.' }
    } else {
        $createdFakeRdr = $true
        $rdrProcess = Start-Process -FilePath $pythonWindow -ArgumentList @(
        ('"' + (Join-Path $PSScriptRoot 'run_live_fake_rdr.py') + '"')) `
        -WorkingDirectory $workspace -WindowStyle Normal -PassThru `
        -RedirectStandardOutput (Join-Path $sessionFolder 'rdr.log') `
        -RedirectStandardError (Join-Path $sessionFolder 'rdr.stderr.log')
    }
    $backendProcess = Start-Process -FilePath $backendExe -ArgumentList @(
        '--bind', $EcsAddress, '--fcs-ip', $FcsAddress, '--fcs-port', "$FcsPort",
        '--stop-file', ('"' + $stopFile + '"'), '--status-file', ('"' + $statusFile + '"')) `
        -WorkingDirectory $workspace -WindowStyle Normal -PassThru
    $deadline = (Get-Date).AddSeconds(15)
    while (-not (Test-Path -LiteralPath $statusFile)) {
        if ($backendProcess.HasExited) { throw 'The live station exited during startup.' }
        if ($rdrProcess.HasExited) { throw "Fake RDR exited; see $sessionFolder/rdr.stderr.log" }
        if ((Get-Date) -gt $deadline) { throw 'Live station startup timed out.' }
        Start-Sleep -Milliseconds 100
    }
    $guiProcess = Start-Process -FilePath $guiExe -WorkingDirectory $workspace -WindowStyle Normal -PassThru
    $session = [ordered]@{
        session_folder = $sessionFolder
        stop_file = $stopFile
        status_file = $statusFile
        backend_pid = $backendProcess.Id
        fake_rdr_pid = $rdrProcess.Id
        gui_pid = $guiProcess.Id
        ecs_address = $EcsAddress
        fcs_address = $FcsAddress
        fcs_port = $FcsPort
        fake_rdr_address = '127.0.0.1:1001'
        fake_rdr_ecs = '127.0.0.1:11000'
        created_at_utc = [DateTime]::UtcNow.ToString('o')
    }
    $session | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $sessionFolder 'session.json') -Encoding UTF8
    $session | ConvertTo-Json
} catch {
    if ($backendProcess -and -not $backendProcess.HasExited) {
        [IO.File]::WriteAllText($stopFile, 'stop')
        if (-not $backendProcess.WaitForExit(4000)) { Stop-Process -Id $backendProcess.Id }
    }
    $createdWindows = @($guiProcess)
    if ($createdFakeRdr) { $createdWindows += $rdrProcess }
    foreach ($windowProcess in $createdWindows) {
        if ($windowProcess -and -not $windowProcess.HasExited) {
            $null = $windowProcess.CloseMainWindow()
            if (-not $windowProcess.WaitForExit(2000)) { Stop-Process -Id $windowProcess.Id }
        }
    }
    throw
}
