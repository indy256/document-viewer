param(
    [ValidateRange(1, 64)][int]$Jobs = 8,
    [ValidateSet('x64', 'arm64')][string]$Architecture = 'x64',
    [string]$Prefix = 'C:/Qt/6.11.2/msvc_lto_64',
    [string]$SourceRoot = 'C:/Qt/src',
    [string]$BuildRoot = 'C:/Qt/build',
    [switch]$UseCurrentEnvironment
)
$ErrorActionPreference = 'Stop'
if ($UseCurrentEnvironment) {
    . "$PSScriptRoot/scripts/invoke-checked.ps1"
} else {
    if ($Architecture -ne 'x64') { throw 'For ARM64, initialize MSVC and pass -UseCurrentEnvironment.' }
    . "$PSScriptRoot/scripts/msvc-env.ps1"
}
if ($env:VSCMD_ARG_TGT_ARCH -ne $Architecture) { throw "Initialize an MSVC $Architecture environment first." }
$modules = @{
    qtbase = 'ef55f427f2c8b410d34f8a7681020a3000cf6866'
    qtsvg = '17ca512f903f935282ebeca496aac5d11ba4199a'
}
foreach ($module in @('qtbase', 'qtsvg')) {
    $source = "$SourceRoot/$module-6.11.2"
    $build = "$BuildRoot/$module-6.11.2-msvc-lto"
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
        $cache = Get-Content -LiteralPath "$build/CMakeCache.txt"
        if ($cache -notcontains 'QT_FEATURE_ltcg:INTERNAL=ON' -or
            $cache -notcontains 'QT_FEATURE_optimize_size:INTERNAL=ON') {
            throw "$module must be configured with LTO and size optimization."
        }
        Invoke-Checked cmake --build . --parallel $Jobs
        Invoke-Checked cmake --install .
    } finally { Pop-Location }
}
Write-Host "Qt with MSVC and LTO installed: $prefix"
exit 0
