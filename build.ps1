# Build thu-courses-ics with CMake.
#
# Reuses an existing build\ cache when there is one (whatever generator it was
# configured with: MinGW Makefiles, Visual Studio, Ninja...), otherwise
# configures build\ with MinGW Makefiles.
#
# Usage: powershell -ExecutionPolicy Bypass -File .\build.ps1

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Build = Join-Path $Root "build"
$Cache = Join-Path $Build "CMakeCache.txt"

function Find-BuiltExe {
    $candidates = @(
        (Join-Path $Build "thu-courses-ics.exe"),
        (Join-Path $Build "Release\thu-courses-ics.exe"),
        (Join-Path $Build "RelWithDebInfo\thu-courses-ics.exe"),
        (Join-Path $Build "Debug\thu-courses-ics.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ }
    if (-not $candidates) { return $null }
    # 新旧产物可能同时存在（例如先 MinGW 后 VS），取最新的那个。
    return ($candidates |
            Sort-Object { (Get-Item -LiteralPath $_).LastWriteTime } -Descending |
            Select-Object -First 1)
}

$generator = ""
if (Test-Path -LiteralPath $Cache) {
    $match = Select-String -Path $Cache -Pattern '^CMAKE_GENERATOR:INTERNAL=(.*)$' | Select-Object -First 1
    if ($match) { $generator = $match.Matches[0].Groups[1].Value.Trim() }
}

Push-Location $Root
try {
    if (Test-Path -LiteralPath $Cache) {
        if ($generator) { Write-Host "复用已有的 build\（生成器：$generator）" -ForegroundColor Cyan }
        else { Write-Host "复用已有的 build\" -ForegroundColor Cyan }
        # 单配置生成器会忽略 --config；多配置生成器（VS）用它选 Release。
        # 不给 VS 生成器传 -j：那等价于 MSBuild /m，个别环境（受限令牌、沙箱）
        # 下会静默失败；MSBuild 自己也会并行编译各个工程。
        if ($generator -like 'Visual Studio*') {
            cmake --build $Build --config Release
        }
        else {
            cmake --build $Build --config Release -j
        }
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed (exit $LASTEXITCODE)" }
    }
    else {
        cmake -S $Root -B $Build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }
        cmake --build $Build -j
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed (exit $LASTEXITCODE)" }
    }
}
finally {
    Pop-Location
}

$exe = Find-BuiltExe
if (-not $exe) { throw "编译完成但没有找到 thu-courses-ics.exe" }

Write-Host ""
Write-Host "OK -> $exe" -ForegroundColor Green
Write-Host "Self test: `"$exe`" --self-test"
