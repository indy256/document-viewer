param([Parameter(Mandatory=$true)][string]$PlanPath)
$ErrorActionPreference = 'Stop'
$stagePath = [IO.Path]::GetFullPath((Split-Path -LiteralPath $PlanPath))
$replacementPath = Join-Path $stagePath 'replacement'
$backupPath = Join-Path $stagePath 'previous.exe'
$committed = $false
$installed = $false
try {
    $plan = Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $targetPath = [IO.Path]::GetFullPath($plan.target)
    if ([IO.Path]::GetDirectoryName($stagePath) -ne [IO.Path]::GetDirectoryName($targetPath) -or
        !(Split-Path -Leaf $stagePath).StartsWith('.dv-update-') -or
        !(Test-Path -LiteralPath $targetPath -PathType Leaf) -or
        !(Test-Path -LiteralPath $replacementPath -PathType Leaf)) {
        throw 'The update paths do not refer to an app and its adjacent staging folder.'
    }
    foreach ($checkedPath in @($stagePath, $targetPath, $replacementPath)) {
        if ((Get-Item -LiteralPath $checkedPath).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Update paths must not be links or junctions.'
        }
    }
    # Hold process handles before acknowledging readiness, avoiding PID reuse.
    $viewer = Get-Process -Id $plan.viewerPid
    $launcher = Get-Process -Id $plan.launcherPid
    $null = $viewer.Handle
    $null = $launcher.Handle
    [IO.File]::WriteAllText((Join-Path $stagePath 'ready'), 'ready')
    $handoffDeadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath (Join-Path $stagePath 'commit'))) {
        if ([DateTime]::UtcNow -gt $handoffDeadline) { throw 'Installation was canceled before handoff.' }
        Start-Sleep -Milliseconds 100
    }
    $committed = $true
    if (!$viewer.WaitForExit(120000) -or !$launcher.WaitForExit(120000)) {
        throw 'The application or portable launcher did not exit. No files were replaced.'
    }
    for ($retry = 0; $retry -lt 30; $retry++) {
        try {
            [IO.File]::Replace($replacementPath, $targetPath, $backupPath)
            $installed = $true
            break
        } catch {
            if ($retry -eq 29) { throw }
            Start-Sleep -Milliseconds 200
        }
    }
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $targetPath
    $start.WorkingDirectory = [IO.Path]::GetDirectoryName($targetPath)
    $start.UseShellExecute = $false
    $restarted = [Diagnostics.Process]::Start($start)
    if ($restarted.WaitForExit(2000)) { throw 'The updated application exited immediately.' }
} catch {
    $reason = $_.Exception.Message
    if ($installed -and (Test-Path -LiteralPath $backupPath)) {
        try {
            [IO.File]::Replace($backupPath, $targetPath, [System.Management.Automation.Language.NullString]::Value)
            $reason += ' The previous version was restored.'
        } catch { $reason += ' Could not restore the previous version: ' + $_.Exception.Message }
    }
    [IO.File]::WriteAllText((Join-Path $stagePath 'failed'), $reason)
    [IO.File]::WriteAllText((Join-Path $stagePath 'helper.log'), $reason)
    Write-Output $reason
    if ($committed -and $viewer.HasExited -and $launcher.HasExited) {
        try {
            $restartInfo = New-Object Diagnostics.ProcessStartInfo
            $restartInfo.FileName = $targetPath
            $restartInfo.UseShellExecute = $false
            $null = [Diagnostics.Process]::Start($restartInfo)
        } catch { Write-Output $_.Exception.Message }
    }
    exit 1
}
# Installation succeeded. Cleanup failures must not roll back a running app.
Set-Location -LiteralPath ([IO.Path]::GetDirectoryName($targetPath))
for ($retry = 0; $retry -lt 30; $retry++) {
    try {
        # stagePath was validated above as this app's adjacent staging directory.
        Remove-Item -LiteralPath $stagePath -Recurse -Force
        break
    } catch {
        if ($retry -eq 29) { throw }
        Start-Sleep -Milliseconds 200
    }
}
