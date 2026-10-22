# 发布相关的共用函数：站点目录识别、复制 .ics、git 提交推送、订阅链接。
# 由 run.ps1 和 weeks.ps1 通过 dot-source 使用（. .\publish.ps1）。
# 约定：函数只依赖参数，不读调用方的脚本变量。

# 读取 .env 里的某个键（不存在返回 $null）。解析规则与程序一致：忽略空行与 # 注释。
# 用 ReadAllLines 而不是 Get-Content：前者按 CR / LF / CRLF 切行，能容忍混用的行尾。
function Get-DotEnvValue {
    param([string] $EnvFile, [string] $Key)
    if (-not $EnvFile -or -not (Test-Path -LiteralPath $EnvFile)) { return $null }
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

# 写入/更新 .env 的某个键：保留原有注释、顺序与换行风格，只改这一行。
function Set-DotEnvValue {
    param([string] $EnvFile, [string] $Key, [string] $Value)

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
        if ($lines.Count -eq 0) { $lines.Add("# 由脚本维护，可手动编辑") }
        $lines.Add($entry)
    }

    $text = ($lines -join $eol) + $eol
    [System.IO.File]::WriteAllText($EnvFile, $text, (New-Object System.Text.UTF8Encoding($hadBom)))
}

function Resolve-PublishPath {
    param([string] $Root, [string] $Path)
    $clean = $Path.Trim().Trim('"').Trim("'")
    if (-not [System.IO.Path]::IsPathRooted($clean)) {
        $clean = Join-Path $Root $clean
    }
    return [System.IO.Path]::GetFullPath($clean)
}

function Test-SiteRepo {
    param([string] $Path)
    if (-not $Path) { return $false }
    return (Test-Path -LiteralPath (Join-Path $Path 'res'))
}

# 同级目录里的 *.github.io 站点仓库，作为 default 关键字的目标。
function Find-SiblingSiteRepo {
    param([string] $Root, [string] $Preferred)
    if ($Preferred -and (Test-Path -LiteralPath $Preferred) -and (Test-SiteRepo -Path $Preferred)) {
        return $Preferred
    }
    $parent = Split-Path -Parent $Root
    if (-not $parent -or -not (Test-Path -LiteralPath $parent)) { return $null }
    $sites = @(Get-ChildItem -LiteralPath $parent -Directory -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -like '*.github.io' -and (Test-SiteRepo -Path $_.FullName) })
    if ($sites.Count -ge 1) { return $sites[0].FullName }
    return $null
}

# 解析发布目标：-Publish > THU_PUBLISH_DIR 环境变量 > .env 的 THU_PUBLISH_DIR > 交互询问。
# 返回 @{ Path = <目录或 $null>; Source = '参数' | '环境变量' | '.env' | 'default' | 'once' | '输入' | '不导出' }
function Resolve-PublishTarget {
    param(
        [string] $Root,
        [string] $EnvFile,
        [string] $Explicit,
        [bool] $Interactive
    )

    if ($Explicit) {
        return @{ Path = (Resolve-PublishPath -Root $Root -Path $Explicit); Source = '参数' }
    }
    if ($env:THU_PUBLISH_DIR) {
        return @{ Path = (Resolve-PublishPath -Root $Root -Path $env:THU_PUBLISH_DIR); Source = '环境变量' }
    }
    $configured = Get-DotEnvValue -EnvFile $EnvFile -Key 'THU_PUBLISH_DIR'
    if ($configured) {
        return @{ Path = (Resolve-PublishPath -Root $Root -Path $configured); Source = '.env' }
    }
    if (-not $Interactive) {
        return @{ Path = $null; Source = '不导出' }
    }

    Write-Host ''
    Write-Host '生成的 ics 要复制到站点仓库吗？复制后会顺带提交并推送。' -ForegroundColor Cyan
    $suggestion = Find-SiblingSiteRepo -Root $Root -Preferred $null
    if ($suggestion) { Write-Host "检测到站点仓库：$suggestion" -ForegroundColor Cyan }
    Write-Host '  default / d   → 用检测到的目录，并写入 .env（以后不再询问）' -ForegroundColor Gray
    Write-Host '  once / o      → 只在本次使用检测到的目录（不写入 .env）' -ForegroundColor Gray
    Write-Host '  目录路径      → 本次导出到该目录（不写入 .env）' -ForegroundColor Gray
    Write-Host '  留空 / n      → 不导出' -ForegroundColor Gray

    $answer = Read-Host '目标'
    if ([string]::IsNullOrWhiteSpace($answer) -or $answer.Trim() -match '^(n|no|否|跳过|skip)$') {
        return @{ Path = $null; Source = '不导出' }
    }
    if ($answer.Trim() -match '^(default|d|默认|记入|记住|save)$') {
        if (-not $suggestion) {
            Write-Host '没有检测到同级站点仓库，无法使用 default；请直接输入目录路径，或留空跳过。' -ForegroundColor Yellow
            return @{ Path = $null; Source = '不导出' }
        }
        return @{ Path = $suggestion; Source = 'default' }
    }
    if ($answer.Trim() -match '^(once|o|本次|这次|临时|just|now)$') {
        if (-not $suggestion) {
            Write-Host '没有检测到同级站点仓库，无法使用 once；请直接输入目录路径，或留空跳过。' -ForegroundColor Yellow
            return @{ Path = $null; Source = '不导出' }
        }
        return @{ Path = $suggestion; Source = 'once' }
    }
    return @{ Path = (Resolve-PublishPath -Root $Root -Path $answer); Source = '输入' }
}

# 校验目标目录；无效时打印原因并返回 $false。
function Confirm-PublishTarget {
    param([string] $Path, [string] $Source)
    if (-not $Path) { return $false }
    if (-not (Test-Path -LiteralPath $Path)) {
        Write-Host "目标目录不存在，已跳过复制：$Path" -ForegroundColor Yellow
        if ($Source -eq '.env') { Write-Host '请修正 .env 里的 THU_PUBLISH_DIR，或加 -Publish 指定。' -ForegroundColor Yellow }
        return $false
    }
    if (-not (Test-SiteRepo -Path $Path)) {
        Write-Host "目标目录里没有 res 子目录，已跳过复制：$Path" -ForegroundColor Yellow
        if ($Source -eq '.env') { Write-Host '请修正 .env 里的 THU_PUBLISH_DIR，或加 -Publish 指定。' -ForegroundColor Yellow }
        return $false
    }
    return $true
}

# 站点根地址：-PublishUrl > .env 的 THU_PUBLISH_URL > 站点根目录的 CNAME > git remote 推导
function Get-SiteBaseUrl {
    param([string] $SiteRoot, [string] $Explicit, [string] $EnvFile)

    if ($Explicit) { return $Explicit.Trim().TrimEnd('/') + '/' }
    $fromEnv = Get-DotEnvValue -EnvFile $EnvFile -Key 'THU_PUBLISH_URL'
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

# 推送成功后打印可订阅的地址；有对照表时带上学号。
function Show-SubscribeLinks {
    param(
        [string] $SiteRoot,
        [string[]] $RelPaths,
        [string] $MapFile,
        [string] $PublishUrl,
        [string] $EnvFile
    )

    if (-not $RelPaths -or $RelPaths.Count -eq 0) { return }

    $codeToId = @{}
    if ($MapFile -and (Test-Path -LiteralPath $MapFile)) {
        foreach ($line in [System.IO.File]::ReadAllLines($MapFile)) {
            if ($line.StartsWith('#') -or $line.Trim().Length -eq 0) { continue }
            $parts = $line -split "`t"
            if ($parts.Count -ge 2 -and $parts[0].Trim()) { $codeToId[$parts[0].Trim()] = $parts[1].Trim() }
        }
    }

    $baseUrl = Get-SiteBaseUrl -SiteRoot $SiteRoot -Explicit $PublishUrl -EnvFile $EnvFile

    Write-Host ''
    Write-Host '📅 订阅链接：' -ForegroundColor Cyan
    foreach ($rel in $RelPaths) {
        $path = $rel -replace '\\', '/'
        $name = [System.IO.Path]::GetFileNameWithoutExtension($path)
        $who = ''
        if ($codeToId.ContainsKey($name)) { $who = '学号 ' + $codeToId[$name] }
        Write-Host ("  {0}{1}" -f $path, $(if ($who) { '    （' + $who + '）' } else { '' }))
        if ($baseUrl) {
            $https = $baseUrl + $path
            Write-Host ("    {0}" -f ($https -replace '^https://', 'webcal://')) -ForegroundColor Green
            Write-Host ("    {0}" -f $https)
        }
    }
    if (-not $baseUrl) {
        Write-Host '  ! 没能推断站点地址，只给出相对路径；用 -PublishUrl 或 .env 的 THU_PUBLISH_URL 指定根地址即可。' -ForegroundColor Yellow
    }
}

# 复制文件到站点仓库并提交推送。
# 返回 @{ Ok = <bool>; Rel = @('res/xxx.ics'); Pushed = <bool> }
function Publish-IcsFiles {
    param(
        [string] $SiteRoot,
        [string[]] $Files,
        [switch] $NoPush
    )

    $result = @{ Ok = $false; Rel = @(); Pushed = $false; Live = $false }
    if (-not $Files -or $Files.Count -eq 0) {
        Write-Host '没有找到生成的 .ics 文件，无法复制。' -ForegroundColor Red
        return $result
    }

    $siteRes = Join-Path $SiteRoot 'res'
    if (-not (Test-Path -LiteralPath $siteRes)) {
        Write-Host "站点仓库里没有 res 目录：$siteRes" -ForegroundColor Red
        return $result
    }

    $rel = New-Object System.Collections.Generic.List[string]
    try {
        foreach ($file in $Files) {
            $name = Split-Path -Leaf $file
            Copy-Item -LiteralPath $file -Destination (Join-Path $siteRes $name) -Force
            $rel.Add("res/$name")
            Write-Host "已复制 $name" -ForegroundColor Green
        }
    }
    catch {
        Write-Host "复制失败：$($_.Exception.Message)" -ForegroundColor Red
        return $result
    }
    Write-Host ''
    $result.Rel = @($rel.ToArray())

    if ($NoPush) {
        Write-Host '已按 -NoPush 跳过提交与推送。' -ForegroundColor Yellow
        $result.Ok = $true
        return $result
    }

    if (-not (Test-Path -LiteralPath (Join-Path $SiteRoot '.git'))) {
        Write-Host '该目录不是 git 仓库，已跳过提交与推送。' -ForegroundColor Yellow
        $result.Ok = $true
        return $result
    }

    # 只处理刚复制的这些文件，避免把站点仓库里其它暂存内容一起提交。
    # 注意：native 命令的 stdout 会进入函数返回值，必须 Out-Host，否则 $result
    # 会被掺进 git 的输出、调用方拿到的就不是哈希表了。
    & git -C $SiteRoot add -- @($result.Rel) | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host "git add 失败（退出码 $LASTEXITCODE）。" -ForegroundColor Red
        return $result
    }

    & git -C $SiteRoot diff --cached --quiet -- @($result.Rel) | Out-Host
    if ($LASTEXITCODE -eq 0) {
        Write-Host '文件内容没有变化，跳过提交与推送。' -ForegroundColor Yellow
        $result.Ok = $true
        $result.Live = $true
        return $result
    }

    & git -C $SiteRoot commit -m 'chore: update ics files' -- @($result.Rel) | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host "git commit 失败（退出码 $LASTEXITCODE）。" -ForegroundColor Red
        return $result
    }

    $branch = (& git -C $SiteRoot rev-parse --abbrev-ref HEAD)
    & git -C $SiteRoot config --get "branch.$branch.remote" 1>$null
    if ($LASTEXITCODE -eq 0) {
        & git -C $SiteRoot push | Out-Host
    }
    else {
        Write-Host "该分支还没有上游，改用 git push -u origin $branch ..." -ForegroundColor Cyan
        & git -C $SiteRoot push -u origin $branch | Out-Host
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "推送失败（退出码 $LASTEXITCODE）：文件已提交在本地，可稍后在站点仓库里手动 git push。" -ForegroundColor Red
        Write-Host '常见原因：远程地址未配置、凭据失效、或远程有新提交需要先 pull。' -ForegroundColor Yellow
        return $result
    }

    $sha = (& git -C $SiteRoot rev-parse --short HEAD)
    Write-Host "已提交并推送：$SiteRoot ($sha)" -ForegroundColor Green
    $result.Ok = $true
    $result.Pushed = $true
    $result.Live = $true
    return $result
}
