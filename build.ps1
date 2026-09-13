param([switch]$Test)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.2\mingw_64\bin;$env:PATH"
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
& $cmake --preset local-qt
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build --preset local-qt
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($Test) {
    & 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --preset local-qt
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
& 'C:\Qt\6.11.2\mingw_64\bin\windeployqt.exe' --release --no-translations --no-system-d3d-compiler --no-opengl-sw build\DocumentViewer.exe
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Copy-Item -LiteralPath build\_deps\pdfium-src\LICENSE -Destination build\PDFium-LICENSE.txt
New-Item -ItemType Directory -Force build\pdfium-licenses | Out-Null
Get-ChildItem -LiteralPath build\_deps\pdfium-src\licenses | Copy-Item -Destination build\pdfium-licenses -Recurse -Force
Write-Host 'Ready: build\DocumentViewer.exe'
