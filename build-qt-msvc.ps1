param([int]$Jobs = 8)
. "$PSScriptRoot/scripts/msvc-env.ps1"
$prefix = 'C:/Qt/6.11.2/msvc_lto_64'
$modules = @{
    qtbase = 'ef55f427f2c8b410d34f8a7681020a3000cf6866'
    qtsvg = '17ca512f903f935282ebeca496aac5d11ba4199a'
}
foreach ($module in @('qtbase', 'qtsvg')) {
    $source = "C:/Qt/src/$module-6.11.2"
    $build = "C:/Qt/build/$module-6.11.2-msvc-lto"
    if (-not (Test-Path "$source/.git")) {
        Invoke-Checked git clone --depth 1 --branch v6.11.2 "https://github.com/qt/$module.git" $source
    }
    $revision = & git -C $source rev-parse HEAD
    if ($LASTEXITCODE -or $revision -ne $modules[$module]) { throw "Unexpected Qt source revision in $source" }
    $changes = & git -C $source status --porcelain
    if ($LASTEXITCODE -or $changes) { throw "Qt source tree must be clean: $source" }
    New-Item -ItemType Directory -Force $build | Out-Null
    Push-Location $build
    try {
        if ($module -eq 'qtbase') {
            Invoke-Checked "$source/configure.bat" -prefix $prefix -release -shared -ltcg -optimize-size -nomake examples -nomake tests '--' -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
        } else {
            Invoke-Checked "$prefix/bin/qt-configure-module.bat" $source '--' -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
        }
        Invoke-Checked cmake --build . --parallel $Jobs
        Invoke-Checked cmake --install .
    } finally { Pop-Location }
}
Write-Host "Qt with MSVC and LTO installed: $prefix"
exit 0
