# Run thu-courses-ics from the repository root, so the default output is
# res/coursesAuto.ics (ignored by git; use -Publish to hand it to the site repo).
#
# Interactive: double-click run.cmd. On the first run it builds, offers to
# create .env from the template, then asks where to copy the .ics:
#
#   default / d  -> copy to the detected *.github.io folder AND remember it in
#                   .env (THU_PUBLISH_DIR), so later runs stop asking
#   once / o     -> copy to the detected *.github.io folder for this run only,
#                   nothing is written to .env
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
# -Publish <dir>   copy the generated .ics to <dir>\res\coursesAuto.ics, then
#                  git add/commit/push that file inside <dir>.
# -NoPush          copy the file but do not commit/push.
# -NonInteractive  never prompt (scheduled tasks / CI). Prompts are skipped as
#                  well when stdin is not a console.
#
# Target resolution order: -Publish, $env:THU_PUBLISH_DIR, THU_PUBLISH_DIR in
# .env, then the interactive answer. Only the "default" answer is persisted.

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

$Exe = Find-BuiltExe

if ($null -eq $Rest) { $Rest = @() }

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

# 读取 .env 里的某个键（不存在返回 $null）。解析规则与程序一致：忽略空行与 # 注释。
# 用 ReadAllLines 而不是 Get-Content：前者同时按 CR / LF / CRLF 切行，能容忍混用的
# 行尾（有人用编辑器粘贴后会出现只有 CR 的行）。
function Get-DotEnvValue {
    param([string] $Key)
    if (-not (Test-Path -LiteralPath $EnvFile)) { return $null }
    foreach ($line in [System.IO.File]::ReadAllLines($EnvFile)) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) { continue }
        $eq = $trimmed.IndexOf('=')
        if ($eq -le 0) { continue }
        if ($trimmed.Substring(0, $eq).Trim() -ne $Key) { continue }
        $value = $trimmed.Substring($eq + 1).Trim().Trim('"').Trim("'")
        if ($value) { return $value }
    }
    return $null
}

# 写入/更新 .env 的某个键：保留原有注释与顺序，只改这一行。
# 行尾统一成文件原有的风格（有 CRLF 就用 CRLF，否则 LF），顺便把混用的 CR 归一化。
function Set-DotEnvValue {
    param([string] $Key, [string] $Value)

    $hadBom = $false
    $eol = "`n"
    $lines = New-Object System.Collections.Generic.List[string]

    if (Test-Path -LiteralPath $EnvFile) {
        $bytes = [System.IO.File]::ReadAllBytes($EnvFile)
        $hadBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
        $raw = [System.IO.File]::ReadAllText($EnvFile)
        if ($raw -match "`r`n") { $eol = "`r`n" }
        foreach ($line in [System.IO.File]::ReadAllLines($EnvFile)) { $lines.Add($line) }
        while ($lines.Count -gt 0 -and $lines[$lines.Count - 1].Trim() -eq '') {
            $lines.RemoveAt($lines.Count - 1)
        }
    }

    $entry = "$Key=$Value"
    $replaced = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $trimmed = $lines[$i].Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) { continue }
        $eq = $trimmed.IndexOf('=')
        if ($eq -le 0) { continue }
        if ($trimmed.Substring(0, $eq).Trim() -eq $Key) {
            $lines[$i] = $entry
            $replaced = $true
            break
        }
    }
    if (-not $replaced) {
        if ($lines.Count -eq 0) { $lines.Add("# 由 run.ps1 维护，可手动编辑") }
        $lines.Add($entry)
    }

    $text = ($lines -join $eol) + $eol
    [System.IO.File]::WriteAllText($EnvFile, $text, (New-Object System.Text.UTF8Encoding($hadBom)))
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

# 同级目录里的 *.github.io 站点仓库，作为 default 关键字的目标。
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

# 3) 决定复制目标：-Publish > 环境变量 > .env > 交互输入。
$TargetSource = $null
if ($Publish) {
    $Publish = Resolve-FullPath -Path $Publish
    $TargetSource = "参数 -Publish"
}
elseif ($env:THU_PUBLISH_DIR) {
    $Publish = Resolve-FullPath -Path $env:THU_PUBLISH_DIR
    $TargetSource = "环境变量"
}
else {
    $configured = Get-DotEnvValue -Key $PublishKey
    if ($configured) {
        $Publish = Resolve-FullPath -Path $configured
        $TargetSource = ".env"
    }
}

if ($Interactive -and $TargetSource -eq ".env") {
    Write-Host ""
    Write-Host "导出目标（来自 .env 的 ${PublishKey}）：$Publish" -ForegroundColor Cyan
    Write-Host "不再询问；如需更改请编辑 .env，或加 -Publish <目录>。" -ForegroundColor DarkGray
}

if ($Interactive -and -not $TargetSource) {
    Write-Host ""
    Write-Host "生成的 ics 要复制到站点仓库吗？复制后会顺带提交并推送。" -ForegroundColor Cyan
    $Suggestion = Find-SiblingSiteRepo -Preferred $null
    if ($Suggestion) { Write-Host "检测到站点仓库：$Suggestion" -ForegroundColor Cyan }
    Write-Host "  default / d   → 用检测到的目录，并写入 .env（以后不再询问）" -ForegroundColor Gray
    Write-Host "  once / o      → 只在本次使用检测到的目录（不写入 .env）" -ForegroundColor Gray
    Write-Host "  目录路径      → 本次导出到该目录（不写入 .env）" -ForegroundColor Gray
    Write-Host "  留空 / n      → 不导出" -ForegroundColor Gray

    $answer = Read-Host "目标"
    if ([string]::IsNullOrWhiteSpace($answer) -or $answer.Trim() -match '^(n|no|否|跳过|skip)$') {
        $Publish = $null
        $TargetSource = "不导出"
    }
    elseif ($answer.Trim() -match '^(once|o|本次|这次|临时|just|now)$') {
        if (-not $Suggestion) {
            Write-Host "没有检测到同级站点仓库，无法使用 once；请直接输入目录路径，或留空跳过。" -ForegroundColor Yellow
            $Publish = $null
            $TargetSource = "不导出"
        }
        else {
            $Publish = $Suggestion
            $TargetSource = "once"
        }
    }
    elseif ($answer.Trim() -match '^(default|d|默认|记入|记住|save)$') {
        if (-not $Suggestion) {
            Write-Host "没有检测到同级站点仓库，无法使用 default；请直接输入目录路径，或留空跳过。" -ForegroundColor Yellow
            $Publish = $null
            $TargetSource = "不导出"
        }
        else {
            $Publish = $Suggestion
            $TargetSource = "default"
        }
    }
    else {
        $Publish = Resolve-FullPath -Path $answer
        $TargetSource = "手动输入"
    }
}

# 校验目标；无效时跳过复制，而不是带着错路径继续。
if ($Publish) {
    if (-not (Test-Path -LiteralPath $Publish)) {
        Write-Host "目标目录不存在，已跳过复制：$Publish" -ForegroundColor Yellow
        if ($TargetSource -eq ".env") {
            Write-Host "请修正 .env 里的 $PublishKey，或加 -Publish 指定。" -ForegroundColor Yellow
        }
        $Publish = $null
    }
    elseif (-not (Test-SiteRepo -Path $Publish)) {
        Write-Host "目标目录里没有 res 子目录，已跳过复制：$Publish" -ForegroundColor Yellow
        if ($TargetSource -eq ".env") {
            Write-Host "请修正 .env 里的 $PublishKey，或加 -Publish 指定。" -ForegroundColor Yellow
        }
        $Publish = $null
    }
}

# 只有 default 答案会写进 .env；手输的路径只对本次生效。
if ($Publish -and $TargetSource -eq "default") {
    try {
        Set-DotEnvValue -Key $PublishKey -Value $Publish
        Write-Host "已写入 .env：$PublishKey=$Publish（下次运行不再询问）" -ForegroundColor Green
    }
    catch {
        Write-Host "写入 .env 失败（不影响本次导出）：$($_.Exception.Message)" -ForegroundColor Yellow
    }
}

# 4) 先记下输出位置相关的参数；真正的文件列表要等程序跑完再扫描
#（生成的文件名是 <学号>.ics，事先不知道会有几个）。
function Get-RunArgValue {
    param([string] $Name)
    for ($i = 0; $i -lt $Rest.Count - 1; $i++) {
        if ($Rest[$i] -eq $Name) { return $Rest[$i + 1] }
    }
    return $null
}

function Resolve-RepoPath {
    param([string] $Path)
    if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

# 站点根地址：-PublishUrl > .env 的 THU_PUBLISH_URL > 站点根目录的 CNAME > git remote 推导
function Get-SiteBaseUrl {
    param([string] $SiteRoot, [string] $Explicit)

    if ($Explicit) { return $Explicit.Trim().TrimEnd('/') + '/' }
    $fromEnv = Get-DotEnvValue -Key 'THU_PUBLISH_URL'
    if ($fromEnv) { return $fromEnv.Trim().TrimEnd('/') + '/' }

    $cname = Join-Path $SiteRoot 'CNAME'
    if (Test-Path -LiteralPath $cname) {
        $hostName = Get-Content -LiteralPath $cname -Raw -ErrorAction SilentlyContinue
        if ($hostName -and $hostName.Trim()) { return 'https://' + $hostName.Trim().TrimEnd('/') + '/' }
    }

    $config = Join-Path $SiteRoot '.git\config'
    if (Test-Path -LiteralPath $config) {
        $match = Select-String -Path $config -Pattern '^\s*url\s*=\s*(\S+)\s*$' | Select-Object -First 1
        if ($match) {
            $raw = $match.Matches[0].Groups[1].Value
            if ($raw -match 'github\.com[:/]+([^/]+)/([^/]+?)(?:\.git)?/?$') {
                $user = $Matches[1]
                $repo = $Matches[2]
                if ($repo -ieq ($user + '.github.io')) { return "https://$user.github.io/" }
                return "https://$user.github.io/$repo/"
            }
        }
    }
    return $null
}

# 推送成功后打印可订阅的地址；一并带上学号（从本地对照表读），方便分辨是谁的。
function Show-SubscribeLinks {
    param([string] $SiteRoot, [string[]] $RelPaths, [string] $MapFile)

    if ($RelPaths.Count -eq 0) { return }

    $codeToId = @{}
    if ($MapFile -and (Test-Path -LiteralPath $MapFile)) {
        foreach ($line in [System.IO.File]::ReadAllLines($MapFile)) {
            if ($line.StartsWith('#') -or $line.Trim().Length -eq 0) { continue }
            $parts = $line -split "`t"
            if ($parts.Count -ge 2 -and $parts[0].Trim()) { $codeToId[$parts[0].Trim()] = $parts[1].Trim() }
        }
    }

    $baseUrl = Get-SiteBaseUrl -SiteRoot $SiteRoot -Explicit $PublishUrl

    Write-Host ""
    Write-Host "📅 订阅链接：" -ForegroundColor Cyan
    foreach ($rel in $RelPaths) {
        $path = $rel -replace '\\', '/'
        $name = [System.IO.Path]::GetFileNameWithoutExtension($path)
        $who = ''
        if ($codeToId.ContainsKey($name)) { $who = '学号 ' + $codeToId[$name] }
        Write-Host ("  {0}{1}" -f $path, $(if ($who) { "    （" + $who + "）" } else { '' }))
        if ($baseUrl) {
            $https = $baseUrl + $path
            Write-Host ("    {0}" -f ($https -replace '^https://', 'webcal://')) -ForegroundColor Green
            Write-Host ("    {0}" -f $https)
        }
    }
    if (-not $baseUrl) {
        Write-Host "  ! 没能推断站点地址，只给出相对路径；用 -PublishUrl 或 .env 的 THU_PUBLISH_URL 指定根地址即可。" -ForegroundColor Yellow
    }
}

$explicitOutput = Get-RunArgValue '--output'
$outDirArg = Get-RunArgValue '--outdir'
$outDir = if ($outDirArg) { Resolve-RepoPath $outDirArg } else { Join-Path $RepoRoot 'res' }
$idMapArg = Get-RunArgValue '--id-map'
$mapFile = if ($idMapArg) { Resolve-RepoPath $idMapArg } else { Join-Path $outDir 'id-map.txt' }

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
# 用时间过滤可以避免把 res\ 里以前留下的旧文件（例如改名前的 coursesAuto.ics）
# 一起发布出去。
$OutFiles = @()
if ($explicitOutput) {
    $explicitPath = Resolve-RepoPath $explicitOutput
    if (Test-Path -LiteralPath $explicitPath) { $OutFiles = @($explicitPath) }
}
elseif (Test-Path -LiteralPath $outDir) {
    $OutFiles = @(Get-ChildItem -LiteralPath $outDir -Filter '*.ics' -File |
                  Where-Object { $_.LastWriteTime -ge $runStarted.AddSeconds(-2) } |
                  Sort-Object Name |
                  ForEach-Object { $_.FullName })
}

# 6) 复制到站点仓库，并自动提交 + 推送。
if (-not $Publish) {
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

if ($OutFiles.Count -eq 0) {
    Write-Host "没有找到生成的 .ics 文件，无法复制。" -ForegroundColor Red
    Write-Host "（注意 --self-test 只跑测试、不生成文件。）" -ForegroundColor Yellow
    exit 1
}

$siteRes = Join-Path $Publish "res"
if (-not (Test-Path -LiteralPath $siteRes)) {
    Write-Host "站点仓库里没有 res 目录：$siteRes" -ForegroundColor Red
    exit 1
}

$RelPaths = @()
try {
    foreach ($file in $OutFiles) {
        $name = Split-Path -Leaf $file
        Copy-Item -LiteralPath $file -Destination (Join-Path $siteRes $name) -Force
        $RelPaths += "res/$name"
        Write-Host "已复制 $name" -ForegroundColor Green
    }
}
catch {
    Write-Host "复制失败：$($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
Write-Host ""

if ($NoPush) {
    Write-Host "已按 -NoPush 跳过提交与推送。" -ForegroundColor Yellow
    exit 0
}

if (-not (Test-Path -LiteralPath (Join-Path $Publish ".git"))) {
    Write-Host "该目录不是 git 仓库，已跳过提交与推送。" -ForegroundColor Yellow
    exit 0
}

# 只处理刚复制的这些文件，避免把站点仓库里其它暂存内容一起提交。
& git -C $Publish add -- @RelPaths
if ($LASTEXITCODE -ne 0) {
    Write-Host "git add 失败（退出码 $LASTEXITCODE）。" -ForegroundColor Red
    exit 1
}

& git -C $Publish diff --cached --quiet -- @RelPaths
if ($LASTEXITCODE -eq 0) {
    Write-Host "文件内容没有变化，跳过提交与推送。" -ForegroundColor Yellow
    Show-SubscribeLinks -SiteRoot $Publish -RelPaths $RelPaths -MapFile $mapFile
    exit 0
}

& git -C $Publish commit -m "chore: update ics files" -- @RelPaths
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
Show-SubscribeLinks -SiteRoot $Publish -RelPaths $RelPaths -MapFile $mapFile
exit 0
