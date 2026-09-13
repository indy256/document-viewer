$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
& .\build.ps1 -Test
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
& $cmake --install build --prefix stage
if ($LASTEXITCODE) { exit $LASTEXITCODE }
py packaging/package.py --platform windows --stage stage --output dist/DocumentViewer.exe --cmake $cmake --cxx C:/Qt/Tools/mingw1310_64/bin/g++.exe --ninja C:/Qt/Tools/Ninja/ninja.exe
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Host 'Portable app: dist\DocumentViewer.exe'
