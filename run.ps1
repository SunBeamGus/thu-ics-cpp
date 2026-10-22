# Run thu-courses-ics from the repository root, so the default output is
# res/coursesAuto.ics (ignored by git; use -Publish to hand it to the site repo).
#
# Interactive: double-click run.cmd. On the first run it builds, offers to
# create .env from the template, then asks where to copy the .ics (remembered
# in .publish-dir). After copying it commits and pushes the site repository
# for you.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 [options...]
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 -Publish ..\your-username.github.io
#   powershell -ExecutionPolicy Bypass -File .\run.ps1 -NonInteractive -Publish ..\site -NoPush
#
# -Publish <dir>   copy the generated .ics to <dir>\res\coursesAuto.ics, then
#                  git add/commit/push that file inside <dir>. The path is
#                  remembered in .publish-dir (git-ignored).
# -NoPush          copy the file but do not commit/push.
# -NonInteractive  never prompt (scheduled tasks / CI). Prompts are skipped as
#                  well when stdin is not a console.
#
# Target resolution order: -Publish, $env:THU_PUBLISH_DIR, .publish-dir,
# a sibling *.github.io repository. Interactive runs always get to override it.

param(
    [string] $Publish,
    [switch] $NoPush,
    [switch] $NonInteractive,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Rest
)

$ErrorActionPreference = "Stop"
$RepoRoot = $PSScriptRoot
$Exe = Join-Path $RepoRoot "build\thu-courses-ics.exe"
$ConfigFile = Join-Path $RepoRoot ".publish-dir"
$SiteRel = "res/coursesAuto.ics"

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

function Get-SavedPublishTarget {
    if (-not (Test-Path -LiteralPath $ConfigFile)) { return $null }
    $value = Get-Content -LiteralPath $ConfigFile -Raw -ErrorAction SilentlyContinue
    if ([string]::IsNullOrWhiteSpace($value)) { return $null }
    return $value.Trim()
}

function Save-PublishTarget {
    param([string] $Path)
    try {
        [System.IO.File]::WriteAllText($ConfigFile, $Path, (New-Object System.Text.UTF8Encoding($false)))
    }
    catch { }
}

function Resolve-FullPath {
    param([string] $Path)
    $clean = $Path.Trim().Trim('"').Trim("'")
    if (-not [System.IO.Path]::IsPathRooted($clean)) {
        $clean = Join-Path $RepoRoot $clean
    }
    return [System.IO.Path]::GetFullPath($clean)
}

function Test-SiteRepo {
    param([string] $Path)
    return (Test-Path -LiteralPath (Join-Path $Path "res"))
}

# 同级目录里的 *.github.io 站点仓库，作为默认建议值。
function Find-SiblingSiteRepo {
    param([string] $Preferred)
    if ($Preferred -and (Test-Path -LiteralPath $Preferred) -and (Test-SiteRepo -Path $Preferred)) {
        return $Preferred
    }
    $parent = Split-Path -Parent $RepoRoot
    if (-not $parent -or -not (Test-Path -LiteralPath $parent)) { return $null }
    $sites = @(Get-ChildItem -LiteralPath $parent -Directory -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -like "*.github.io" -and (Test-SiteRepo -Path $_.FullName) })
    if ($sites.Count -ge 1) { return $sites[0].FullName }
    return $null
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

# 3) 决定复制目标：参数 > 环境变量 > 记住的上次选择 > 同级站点仓库。
$TargetSource = $null
if ($Publish) {
    $Publish = Resolve-FullPath -Path $Publish
    $TargetSource = "-Publish"
}
elseif ($env:THU_PUBLISH_DIR) {
    $Publish = Resolve-FullPath -Path $env:THU_PUBLISH_DIR
    $TargetSource = "THU_PUBLISH_DIR"
}
else {
    $saved = Get-SavedPublishTarget
    if ($saved) {
        $Publish = Resolve-FullPath -Path $saved
        $TargetSource = ".publish-dir"
    }
}

$Suggestion = Find-SiblingSiteRepo -Preferred $Publish

if ($Interactive) {
    Write-Host ""
    if ($Publish) {
        Write-Host "上次的复制目标：$Publish" -ForegroundColor Cyan
        $answer = Read-Host "回车继续用它；输入 n 跳过复制；或直接输入新的目标目录"
    }
    else {
        Write-Host "把生成的 ics 复制到站点仓库（含 res 子目录的那一层），随后自动提交并推送。" -ForegroundColor Cyan
        if ($Suggestion) { Write-Host "检测到：$Suggestion" -ForegroundColor Cyan }
        $answer = Read-Host "目标目录（留空跳过复制）"
    }

    if (-not [string]::IsNullOrWhiteSpace($answer)) {
        $trimmed = $answer.Trim()
        if ($trimmed -match '^(n|no|否|跳过|skip)$') {
            $Publish = $null
            $TargetSource = "跳过"
        }
        else {
            $Publish = Resolve-FullPath -Path $trimmed
            $TargetSource = "手动输入"
        }
    }
    elseif (-not $Publish -and $Suggestion) {
        $Publish = $Suggestion
        $TargetSource = "检测到的同级仓库"
    }
}

if ($Publish) {
    if (-not (Test-Path -LiteralPath $Publish)) {
        Write-Host "目标目录不存在，已跳过复制：$Publish" -ForegroundColor Yellow
        $Publish = $null
    }
    elseif (-not (Test-SiteRepo -Path $Publish)) {
        Write-Host "目标目录里没有 res 子目录，已跳过复制：$Publish" -ForegroundColor Yellow
        $Publish = $null
    }
}

# 4) 计算输出路径，供复制使用。
$OutFile = Join-Path $RepoRoot $SiteRel
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

# 6) 复制到站点仓库，并自动提交 + 推送。
if (-not $Publish) {
    Write-Host ""
    Write-Host "已生成 $OutFile（未复制到站点仓库）。" -ForegroundColor Green
    exit 0
}

if (-not (Test-Path -LiteralPath $OutFile)) {
    Write-Host "未找到输出文件，无法复制：$OutFile" -ForegroundColor Red
    Write-Host "（注意 --self-test 只跑测试、不生成文件。）" -ForegroundColor Yellow
    exit 1
}

$Target = Join-Path $Publish "res\coursesAuto.ics"
try {
    Copy-Item -LiteralPath $OutFile -Destination $Target -Force
}
catch {
    Write-Host "复制失败：$($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
Write-Host ""
Write-Host "已复制到 $Target" -ForegroundColor Green

# 复制成功后才记住目标，避免把无效路径写进 .publish-dir。
if ($TargetSource -ne ".publish-dir") {
    Save-PublishTarget -Path $Publish
}

if ($NoPush) {
    Write-Host "已按 -NoPush 跳过提交与推送。" -ForegroundColor Yellow
    exit 0
}

if (-not (Test-Path -LiteralPath (Join-Path $Publish ".git"))) {
    Write-Host "该目录不是 git 仓库，已跳过提交与推送。" -ForegroundColor Yellow
    exit 0
}

# 只处理这一个文件，避免把站点仓库里其它暂存内容一起提交。
& git -C $Publish add -- $SiteRel
if ($LASTEXITCODE -ne 0) {
    Write-Host "git add 失败（退出码 $LASTEXITCODE）。" -ForegroundColor Red
    exit 1
}

& git -C $Publish diff --cached --quiet -- $SiteRel
if ($LASTEXITCODE -eq 0) {
    Write-Host "文件内容没有变化，跳过提交与推送。" -ForegroundColor Yellow
    exit 0
}

& git -C $Publish commit -m "chore: update res/coursesAuto.ics" -- $SiteRel
if ($LASTEXITCODE -ne 0) {
    Write-Host "git commit 失败（退出码 $LASTEXITCODE）。" -ForegroundColor Red
    exit 1
}

$branch = (& git -C $Publish rev-parse --abbrev-ref HEAD)
& git -C $Publish config --get "branch.$branch.remote" 1>$null
if ($LASTEXITCODE -eq 0) {
    & git -C $Publish push
}
else {
    Write-Host "该分支还没有上游，改用 git push -u origin $branch ..." -ForegroundColor Cyan
    & git -C $Publish push -u origin $branch
}
if ($LASTEXITCODE -ne 0) {
    Write-Host "推送失败（退出码 $LASTEXITCODE）：文件已提交在本地，可稍后在站点仓库里手动 git push。" -ForegroundColor Red
    Write-Host "常见原因：远程地址未配置、凭据失效、或远程有新提交需要先 pull。" -ForegroundColor Yellow
    exit 1
}

$sha = (& git -C $Publish rev-parse --short HEAD)
Write-Host "已提交并推送：$Publish ($sha)" -ForegroundColor Green
exit 0
