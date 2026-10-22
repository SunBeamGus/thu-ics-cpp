# Build thu-courses-ics (MinGW-w64 + CMake), output in .\build.
# Usage: powershell -ExecutionPolicy Bypass -File .\build.ps1

$ErrorActionPreference = "Stop"
$Build = Join-Path $PSScriptRoot "build"

Push-Location $PSScriptRoot
try {
    cmake -S $PSScriptRoot -B $Build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }

    cmake --build $Build -j
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed (exit $LASTEXITCODE)" }
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "OK -> $(Join-Path $Build 'thu-courses-ics.exe')" -ForegroundColor Green
Write-Host "Self test: $(Join-Path $Build 'thu-courses-ics.exe') --self-test"
