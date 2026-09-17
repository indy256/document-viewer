param([Parameter(Mandatory=$true)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([IO.Path]::GetFullPath($BuildDirectory)) ('updater-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$installerPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../packaging/updates/install.ps1'))
$powershell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$fixture = Join-Path $testRoot 'fixture.exe'
Add-Type -OutputAssembly $fixture -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
using System.Diagnostics;
using System.Threading;
public class UpdateFixture {
    public static void Main(string[] args) {
        if (args.Length == 0)
            File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "restarted"), Process.GetCurrentProcess().Id.ToString());
        Thread.Sleep(60000);
    }
}
'@
function Wait-File([string]$path, [int]$seconds = 15) {
    $until = [DateTime]::UtcNow.AddSeconds($seconds)
    while (!(Test-Path -LiteralPath $path)) {
        if ([DateTime]::UtcNow -ge $until) { throw "Timed out waiting for $path" }
        Start-Sleep -Milliseconds 100
    }
}
foreach ($scenario in @('install', 'rollback', 'invalid-target', 'cancel')) {
    $caseRoot = Join-Path $testRoot ("space ' dollar `$ " + $scenario)
    $staging = Join-Path $caseRoot '.dv-update-test'
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    $target = Join-Path $caseRoot 'viewer.exe'
    Copy-Item -LiteralPath $fixture -Destination $target
    $append = [IO.File]::OpenWrite($target)
    $null = $append.Seek(0, [IO.SeekOrigin]::End)
    $append.WriteByte(42)
    $append.Dispose()
    $original = (Get-FileHash -LiteralPath $target).Hash
    $replacement = Join-Path $staging 'replacement'
    if ($scenario -eq 'rollback') { [IO.File]::WriteAllText($replacement, 'not an executable') }
    else { Copy-Item -LiteralPath $fixture -Destination $replacement }
    $expected = (Get-FileHash -LiteralPath $replacement).Hash
    $viewer = Start-Process -FilePath $fixture -ArgumentList '--wait' -WindowStyle Hidden -PassThru
    $launcher = Start-Process -FilePath $target -ArgumentList '--wait' -WindowStyle Hidden -PassThru
    $worker = $null
    try {
        $plan = @{target=$target; viewerPid=$viewer.Id; launcherPid=$launcher.Id}
        if ($scenario -eq 'invalid-target') { $plan.target = $fixture }
        $planPath = Join-Path $staging 'plan.json'
        $plan | ConvertTo-Json | Set-Content -LiteralPath $planPath -Encoding UTF8
        Copy-Item -LiteralPath $installerPath -Destination (Join-Path $staging 'install.ps1')
        $worker = Start-Process -FilePath $powershell -WindowStyle Hidden -PassThru -ArgumentList @(
            '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
            ('"' + (Join-Path $staging 'install.ps1') + '"'), '-PlanPath', ('"' + $planPath + '"'))
        $null = $worker.Handle
        if ($scenario -eq 'invalid-target') {
            if (!$worker.WaitForExit(10000) -or $worker.ExitCode -eq 0) { throw 'Invalid target was accepted' }
        } else {
            Wait-File (Join-Path $staging 'ready')
            if ((Get-FileHash -LiteralPath $target).Hash -ne $original) { throw 'Target changed before commit' }
            if ($scenario -eq 'cancel') {
                if (!$worker.WaitForExit(35000) -or $worker.ExitCode -eq 0) { throw 'Uncommitted update was not canceled' }
            } else {
                [IO.File]::WriteAllText((Join-Path $staging 'commit'), 'install')
                $viewer.Kill(); $viewer.WaitForExit()
                Start-Sleep -Milliseconds 400
                if ((Get-FileHash -LiteralPath $target).Hash -ne $original) { throw 'Did not wait for launcher exit' }
                $launcher.Kill(); $launcher.WaitForExit()
                if (!$worker.WaitForExit(15000)) { throw 'Installer timed out' }
                if ($scenario -eq 'install') {
                    if ($worker.ExitCode -ne 0 -or (Get-FileHash -LiteralPath $target).Hash -ne $expected) { throw 'Replacement failed' }
                    Wait-File (Join-Path $staging 'installed')
                } elseif ($worker.ExitCode -eq 0) { throw 'Broken replacement reported success' }
                Wait-File (Join-Path $caseRoot 'restarted')
            }
        }
        if ($scenario -ne 'install' -and (Get-FileHash -LiteralPath $target).Hash -ne $original) {
            throw 'Original application was not preserved'
        }
        Write-Output "PASS: $scenario"
    } finally {
        foreach ($process in @($worker, $viewer, $launcher)) {
            if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        }
        $marker = Join-Path $caseRoot 'restarted'
        if (Test-Path -LiteralPath $marker) {
            Stop-Process -Id ([int][IO.File]::ReadAllText($marker)) -ErrorAction SilentlyContinue
        }
    }
}
