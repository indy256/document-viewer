param([Parameter(Mandatory)][string]$Executable)

$previousPath = $env:PATH
$previousPlatformPlugins = $env:QT_QPA_PLATFORM_PLUGIN_PATH
$previousPlugins = $env:QT_PLUGIN_PATH
try {
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
    $env:QT_PLUGIN_PATH = $null
    $app = Start-Process -FilePath $Executable -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru
    if (-not $app.WaitForExit(120000)) {
        Stop-Process -Id $app.Id
        throw 'Portable launch timed out'
    }
    if ($app.ExitCode -ne 0) { throw "Portable launch failed: $($app.ExitCode)" }
} finally {
    $env:PATH = $previousPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $previousPlatformPlugins
    $env:QT_PLUGIN_PATH = $previousPlugins
}
