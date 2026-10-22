# Run thu-courses-ics from the repository root, so the default output is
# res/<code>.ics (ignored by git; use -Publish to hand it to the site repo).
#
# Interactive: double-click run.cmd. On the first run it builds, offers to
# create .env from the template, then asks where to copy the .ics:
#
#   default / d  -> copy to the detected *.github.io folder AND remember it in
#                   .env (THU_PUBLISH_DIR), so later runs stop asking
#   once / o     -> copy to the detected *.github.io folder for this run only
#   a path       -> copy there for this run only, nothing is written to .env
#   empty / n    -> do not copy
#
# After copying it commits and pushes the site repository for you.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 [options...]
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 -Publish ..\your-username.github.io
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 -NonInteractive -NoPush
#
# -Publish <dir>   copy the generated .ics to <dir>\res\, then git
#                  add/commit/push that file inside <dir>.
# -PublishUrl <u>  site root URL used when printing subscribe links.
# -NoPush          copy the file but do not commit/push.
# -NonInteractive  never prompt (scheduled tasks / CI).
#
# 站点目录识别、复制与推送逻辑在 publish.ps1（weeks.ps1 也用同一套）。

param(
    [string] $Publish,
    [string] $PublishUrl,
    [switch] $NoPush,
    [switch] $NonInteractive,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Rest
)

$ErrorActionPreference = "Stop"
$RepoRoot = $PSScriptRoot
$EnvFile = Join-Path $RepoRoot ".env"
$EnvExample = Join-Path $RepoRoot ".env.example"
$PublishKey = "THU_PUBLISH_DIR"

. (Join-Path $RepoRoot "publish.ps1")

if ($null -eq $Rest) { $Rest = @() }

# 单配置生成器把 exe 放在 build\，多配置生成器（Visual Studio）放在 build\Release\。
# 两种都找，并优先用最新的那个。
function Find-BuiltExe {
    $candidates = @(
        (Join-Path $RepoRoot "build\thu-courses-ics.exe"),
        (Join-Path $RepoRoot "build\Release\thu-courses-ics.exe"),
        (Join-Path $RepoRoot "build\RelWithDebInfo\thu-courses-ics.exe"),
        (Join-Path $RepoRoot "build\Debug\thu-courses-ics.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ }
    if (-not $candidates) { return $null }
    return ($candidates |
            Sort-Object { (Get-Item -LiteralPath $_).LastWriteTime } -Descending |
            Select-Object -First 1)
}

function Get-RunArgValue {
    param([string] $Name)
    for ($i = 0; $i -lt $Rest.Count - 1; $i++) {
        if ($Rest[$i] -eq $Name) { return $Rest[$i + 1] }
    }
    return $null
}

$Exe = Find-BuiltExe

# 这些用法不需要凭据，也就没必要追问 .env / 发布目标。
$NoCredentialRun = $false
$SelfTestOrHelp = $false
foreach ($arg in $Rest) {
    if ($arg -in @('--self-test', '--help', '-h', '--offline')) { $NoCredentialRun = $true }
    if ($arg -in @('--self-test', '--help', '-h')) { $SelfTestOrHelp = $true }
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
if (-not $Exe) {
    Write-Host "未找到可执行文件，正在编译（需要 CMake + MinGW-w64 或 Visual Studio）…" -ForegroundColor Cyan
    try {
        & (Join-Path $RepoRoot "build.ps1")
    }
    catch {
        Write-Host "编译失败：$($_.Exception.Message)" -ForegroundColor Red
        Write-Host "请确认已安装 CMake ≥ 3.20 与 MinGW-w64（见 README 的「构建」一节）。" -ForegroundColor Yellow
        exit 1
    }
    $Exe = Find-BuiltExe
    if (-not $Exe) {
        Write-Host "编译完成但没有找到 thu-courses-ics.exe。" -ForegroundColor Red
        exit 1
    }
}

# 3) 决定复制目标（共用逻辑见 publish.ps1）。
$target = Resolve-PublishTarget -Root $RepoRoot -EnvFile $EnvFile -Explicit $Publish -Interactive $Interactive
$PublishPath = $target.Path

if ($Interactive -and $target.Source -eq ".env" -and $PublishPath) {
    Write-Host ""
    Write-Host "导出目标（来自 .env 的 ${PublishKey}）：$PublishPath" -ForegroundColor Cyan
    Write-Host "不再询问；如需更改请编辑 .env，或加 -Publish <目录>。" -ForegroundColor DarkGray
}

if ($PublishPath -and -not (Confirm-PublishTarget -Path $PublishPath -Source $target.Source)) {
    $PublishPath = $null
}

# 只有 default 答案会写进 .env；手输的路径只对本次生效。
if ($PublishPath -and $target.Source -eq "default") {
    try {
        Set-DotEnvValue -EnvFile $EnvFile -Key $PublishKey -Value $PublishPath
        Write-Host "已写入 .env：$PublishKey=$PublishPath（下次运行不再询问）" -ForegroundColor Green
    }
    catch {
        Write-Host "写入 .env 失败（不影响本次导出）：$($_.Exception.Message)" -ForegroundColor Yellow
    }
}

# 4) 先记下输出位置相关的参数；文件列表要等程序跑完再扫描。
$explicitOutput = Get-RunArgValue '--output'
$outDirArg = Get-RunArgValue '--outdir'
$outDir = if ($outDirArg) { Resolve-PublishPath -Root $RepoRoot -Path $outDirArg } else { Join-Path $RepoRoot 'res' }
$idMapArg = Get-RunArgValue '--id-map'
$mapFile = if ($idMapArg) { Resolve-PublishPath -Root $RepoRoot -Path $idMapArg } else { Join-Path $outDir 'id-map.txt' }

# 5) 运行。
$runStarted = Get-Date
Push-Location $RepoRoot
try {
    & $Exe @Rest
    $code = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($code -ne 0) { exit $code }

# --self-test / --help 不生成文件，后面的“已生成/已复制”提示就不适用了。
if ($SelfTestOrHelp) { exit $code }

# 现在扫描输出目录，只收本次刚写过的 .ics（单人 1 个，批量 N 个）。
# 用时间过滤可以避免把 res\ 里以前留下的旧文件一起发布出去。
$OutFiles = @()
if ($explicitOutput) {
    $explicitPath = Resolve-PublishPath -Root $RepoRoot -Path $explicitOutput
    if (Test-Path -LiteralPath $explicitPath) { $OutFiles = @($explicitPath) }
}
elseif (Test-Path -LiteralPath $outDir) {
    $OutFiles = @(Get-ChildItem -LiteralPath $outDir -Filter '*.ics' -File |
                  Where-Object { $_.LastWriteTime -ge $runStarted.AddSeconds(-2) } |
                  Sort-Object Name |
                  ForEach-Object { $_.FullName })
}

# 6) 复制到站点仓库，并自动提交 + 推送。
if (-not $PublishPath) {
    Write-Host ""
    if ($OutFiles.Count -gt 0) {
        Write-Host ("已生成 {0} 个课表文件（未复制到站点仓库）：" -f $OutFiles.Count) -ForegroundColor Green
        foreach ($file in $OutFiles) { Write-Host "  $file" }
    }
    else {
        Write-Host "已生成课表（未复制到站点仓库）。" -ForegroundColor Green
    }
    exit 0
}

$publishResult = Publish-IcsFiles -SiteRoot $PublishPath -Files $OutFiles -NoPush:$NoPush
if (-not $publishResult.Ok) { exit 1 }
if ($publishResult.Live) {
    Show-SubscribeLinks -SiteRoot $PublishPath -RelPaths $publishResult.Rel -MapFile $mapFile `
        -PublishUrl $PublishUrl -EnvFile $EnvFile
}
exit 0
