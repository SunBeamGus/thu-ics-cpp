# Run thu-courses-ics from the repository root, so the default output is
# res/coursesAuto.ics (ignored by git; use -Publish to hand it to the site repo).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 [options...]
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 -Publish ..\your-username.github.io
#
# -Publish <dir>  run normally, then copy the generated .ics to
#                 <dir>\res\coursesAuto.ics (the GitHub Pages repository).
#                 It only copies the file; commit and push it yourself.

param(
    [string] $Publish,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Rest
)

$ErrorActionPreference = "Stop"
$RepoRoot = $PSScriptRoot
$Exe = Join-Path $RepoRoot "build\thu-courses-ics.exe"

if ($null -eq $Rest) { $Rest = @() }

if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Host "Executable not found, building first..."
    & (Join-Path $RepoRoot "build.ps1")
    if (-not (Test-Path -LiteralPath $Exe)) { throw "Still missing: $Exe" }
}

# Work out where the program will write, so -Publish can copy it afterwards.
$OutFile = Join-Path $RepoRoot "res\coursesAuto.ics"
for ($i = 0; $i -lt $Rest.Count - 1; $i++) {
    if ($Rest[$i] -eq "--output") { $OutFile = $Rest[$i + 1] }
}
if (-not [System.IO.Path]::IsPathRooted($OutFile)) {
    $OutFile = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutFile))
}

Push-Location $RepoRoot
try {
    & $Exe @Rest
    $code = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($code -ne 0) { exit $code }

if ($Publish) {
    $PublishRoot = [System.IO.Path]::GetFullPath($Publish)
    $ResDir = Join-Path $PublishRoot "res"
    if (-not (Test-Path -LiteralPath $ResDir)) {
        throw "不是可发布的站点仓库（缺少 res 目录）：$PublishRoot"
    }
    if (-not (Test-Path -LiteralPath $OutFile)) { throw "未找到输出文件：$OutFile" }

    $Target = Join-Path $ResDir "coursesAuto.ics"
    Copy-Item -LiteralPath $OutFile -Destination $Target -Force
    Write-Host ""
    Write-Host "已复制到 $Target" -ForegroundColor Green
    Write-Host "接下来在网站仓库里提交并推送："
    Write-Host "  cd `"$PublishRoot`""
    Write-Host "  git add res/coursesAuto.ics"
    Write-Host "  git commit -m `"chore: update coursesAuto.ics`""
    Write-Host "  git push"
}

exit 0
