$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Install Visual Studio with Desktop development with C++ first.' }
Import-Module "$visualStudio\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $visualStudio -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$python = & py -3 -c 'import sys; print(sys.executable)'
if ($LASTEXITCODE) { throw 'Python 3 is required.' }
$env:PATH = "C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;$(Split-Path $python);$env:PATH"
function Invoke-Checked {
    param([string]$Program, [Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE) { throw "$Program failed with exit code $LASTEXITCODE" }
}
