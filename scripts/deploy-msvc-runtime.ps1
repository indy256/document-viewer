param(
    [Parameter(Mandatory=$true)][string]$Stage,
    [ValidateSet('x64', 'arm64')][string]$Architecture = 'x64'
)
$ErrorActionPreference = 'Stop'
$bin = Join-Path (Resolve-Path -LiteralPath $Stage).Path 'bin'
if (-not (Test-Path -LiteralPath "$bin/DocumentViewer.exe")) { throw 'Deploy the viewer before its runtime.' }
if (-not $env:VCToolsRedistDir) { throw 'Initialize the MSVC environment first.' }
$crtDirectories = @(Get-ChildItem "$env:VCToolsRedistDir/$Architecture/Microsoft.VC*.CRT" -Directory)
if ($crtDirectories.Count -ne 1) { throw "Cannot locate the MSVC $Architecture runtime DLL directory." }
$crt = $crtDirectories[0].FullName
$requiredLibraries = @('msvcp140.dll', 'vcruntime140.dll')
if ($Architecture -eq 'x64') { $requiredLibraries += 'vcruntime140_1.dll' }
foreach ($required in $requiredLibraries) {
    if (-not (Test-Path -LiteralPath "$crt/$required")) { throw "Missing compiler runtime: $required" }
}
Get-ChildItem -LiteralPath $crt -Filter '*.dll' | Copy-Item -Destination $bin -Force
# The portable launcher does not execute redistributable installers.
Get-ChildItem -LiteralPath $bin -Filter 'vc_redist.*.exe' | ForEach-Object {
    Remove-Item -LiteralPath $_.FullName
}
Write-Host "Deployed MSVC $Architecture runtime DLLs to $bin"
