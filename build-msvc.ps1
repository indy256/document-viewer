param([switch]$Test, [switch]$Package, [int]$Jobs = 8)
. "$PSScriptRoot/scripts/msvc-env.ps1"
Set-Location $PSScriptRoot
$qt = 'C:/Qt/6.11.2/msvc_lto_64'
if (-not (Test-Path "$qt/lib/cmake/Qt6/Qt6Config.cmake")) {
    throw 'Build Qt first with build-qt-msvc.cmd.'
}
$env:PATH = "$qt/bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$qt/plugins/platforms"
Invoke-Checked cmake --preset local-msvc-lto
Invoke-Checked cmake --build --preset local-msvc-lto --parallel $Jobs
if ($Test -or $Package) { Invoke-Checked ctest --preset local-msvc-lto }
Invoke-Checked "$qt/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw build/msvc/DocumentViewer.exe
if ($Package) {
    Invoke-Checked cmake --install build/msvc --prefix stage/msvc
    & "$PSScriptRoot/scripts/deploy-msvc-runtime.ps1" -Stage stage/msvc -Architecture x64
    Invoke-Checked py packaging/package.py --platform windows --stage stage/msvc --output dist/dv-windows-x64.exe --build-dir build/portable-msvc --cxx cl
    $previousPath = $env:PATH
    $previousPlugins = $env:QT_QPA_PLATFORM_PLUGIN_PATH
    try {
        $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
        $app = Start-Process -FilePath 'dist/dv-windows-x64.exe' -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru
    } finally {
        $env:PATH = $previousPath
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $previousPlugins
    }
    if (-not $app.WaitForExit(120000)) { Stop-Process -Id $app.Id; throw 'Portable launch timed out' }
    if ($app.ExitCode -ne 0) { throw "Portable launch failed: $($app.ExitCode)" }
    Write-Host 'Portable app: dist/dv-windows-x64.exe'
}
Write-Host 'Ready: build/msvc/DocumentViewer.exe'
exit 0
