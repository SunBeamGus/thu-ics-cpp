# Run thu-courses-ics from the repository root, so the default output is
# res/coursesAuto.ics (ignored by git; use -Publish to hand it to the site repo).
#
# Interactive: double-click run.cmd. It builds on the first run, offers to
# create .env from the template, and can copy the result into a sibling
# "<name>.github.io" repository when one is detected.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 [options...]
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 -Publish ..\your-username.github.io
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 -NonInteractive --self-test
#
# -Publish <dir>   run normally, then copy the generated .ics to
#                  <dir>\res\coursesAuto.ics (the static site repository).
#                  It only copies the file; commit and push it yourself.
# -NonInteractive  never prompt (scheduled tasks / CI). Prompts are skipped
#                  automatically as well when stdin is not a console.

param(
    [string] $Publish,
    [switch] $NonInteractive,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Rest
)

$ErrorActionPreference = "Stop"
$RepoRoot = $PSScriptRoot
$Exe = Join-Path $RepoRoot "build\thu-courses-ics.exe"

if ($null -eq $Rest) { $Rest = @() }

# 这些用法不需要凭据，也就没必要追问 .env / 发布目标。
$NoCredentialRun = $false
foreach ($arg in $Rest) {
    if ($arg -in @('--self-test', '--help', '-h', '--offline')) { $NoCredentialRun = $true }
}

$Interactive = (-not $NonInteractive) -and (-not $NoCredentialRun) -and
               [Environment]::UserInteractive -and (-not [Console]::IsInputRedirected)

function Read-YesNo {
    param([string] $Question, [bool] $DefaultYes)
    $hint = if ($DefaultYes) { "[Y/n]" } else { "[y/N]" }
    while ($true) {
        $answer = Read-Host "$Question $hint"
        if ([string]::IsNullOrWhiteSpace($answer)) { return $DefaultYes }
        switch -Regex ($answer.Trim()) {
            '^(y|yes|是)$' { return $true }
            '^(n|no|否)$'  { return $false }
        }
    }
}

# 1) 首次运行：缺 .env 时从模板生成，并打开记事本填写。
$EnvFile = Join-Path $RepoRoot ".env"
$EnvExample = Join-Path $RepoRoot ".env.example"
if ($Interactive -and -not (Test-Path -LiteralPath $EnvFile) -and (Test-Path -LiteralPath $EnvExample)) {
    Write-Host "还没有配置凭据：$EnvFile 不存在。" -ForegroundColor Yellow
    if (Read-YesNo -Question "是否由 .env.example 生成 .env 并用记事本填写？" -DefaultYes $true) {
        Copy-Item -LiteralPath $EnvExample -Destination $EnvFile
        Start-Process notepad.exe -ArgumentList "`"$EnvFile`""
        Write-Host "请填好 THU_USER_ID / THU_PASSWORD 后保存。" -ForegroundColor Yellow
        Read-Host "填好后按回车继续" | Out-Null
    }
}

# 2) 首次运行：编译。
if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Host "未找到可执行文件，正在编译（需要 CMake + MinGW-w64）…" -ForegroundColor Cyan
    try {
        & (Join-Path $RepoRoot "build.ps1")
    }
    catch {
        Write-Host "编译失败：$($_.Exception.Message)" -ForegroundColor Red
        Write-Host "请确认已安装 CMake ≥ 3.20 与 MinGW-w64（见 README 的「构建」一节）。" -ForegroundColor Yellow
        exit 1
    }
    if (-not (Test-Path -LiteralPath $Exe)) {
        Write-Host "编译结束但未找到：$Exe" -ForegroundColor Red
        exit 1
    }
}

# 3) 可选：发现同级站点仓库时，询问是否顺带复制过去。
if ($Interactive -and -not $Publish) {
    $parent = Split-Path -Parent $RepoRoot
    if ($parent -and (Test-Path -LiteralPath $parent)) {
        $sites = @(Get-ChildItem -LiteralPath $parent -Directory -ErrorAction SilentlyContinue |
                   Where-Object {
                       $_.Name -like "*.github.io" -and
                       (Test-Path -LiteralPath (Join-Path $_.FullName "res"))
                   })
        if ($sites.Count -eq 1) {
            $site = $sites[0]
            if (Read-YesNo -Question "生成后是否同时复制到 $($site.Name)\res\ ？" -DefaultYes $false) {
                $Publish = $site.FullName
            }
        }
    }
}

# 4) 计算输出路径，供 -Publish 使用。
$OutFile = Join-Path $RepoRoot "res\coursesAuto.ics"
for ($i = 0; $i -lt $Rest.Count - 1; $i++) {
    if ($Rest[$i] -eq "--output") { $OutFile = $Rest[$i + 1] }
}
if (-not [System.IO.Path]::IsPathRooted($OutFile)) {
    $OutFile = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutFile))
}

# 5) 运行。
Push-Location $RepoRoot
try {
    & $Exe @Rest
    $code = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($code -ne 0) { exit $code }

# 6) 复制到站点仓库。
if ($Publish) {
    try {
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
    catch {
        Write-Host "复制失败：$($_.Exception.Message)" -ForegroundColor Red
        exit 1
    }
}

exit 0
