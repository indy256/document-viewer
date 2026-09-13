param(
    [Parameter(Mandatory=$true)][ValidateSet('x64', 'arm64')][string]$Architecture,
    [Parameter(Mandatory=$true)][ValidateSet(17, 18)][int]$VisualStudioMajor
)
$ErrorActionPreference = 'Stop'
$before = @{}
Get-ChildItem Env: | ForEach-Object { $before[$_.Name] = $_.Value }
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$versionRange = "[$VisualStudioMajor,$($VisualStudioMajor + 1))"
$visualStudio = & $vswhere -latest -products * -version $versionRange -property installationPath
if ($LASTEXITCODE -or -not $visualStudio) { throw "Visual Studio $VisualStudioMajor is not installed." }
Import-Module "$visualStudio/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
$hostArchitecture = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'arm64' } else { 'x64' }
Enter-VsDevShell -VsInstallPath $visualStudio -SkipAutomaticLocation -DevCmdArguments "-arch=$Architecture -host_arch=$hostArchitecture"
if ($env:VSCMD_ARG_TGT_ARCH -ne $Architecture) { throw 'MSVC target architecture was not initialized.' }
Get-Command cl.exe -ErrorAction Stop | Select-Object -ExpandProperty Source
if (-not $env:GITHUB_ENV) { throw 'This setup script must run inside GitHub Actions.' }
# Persist the developer environment, including PATH, for subsequent job steps.
Get-ChildItem Env: | Where-Object { $before[$_.Name] -ne $_.Value } | ForEach-Object {
    if ($_.Value -match "[\r\n]") { throw "Unexpected multiline environment variable: $($_.Name)" }
    "$($_.Name)=$($_.Value)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
}
