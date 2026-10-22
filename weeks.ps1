# 生成「按周次」的全天事件日历（.ics）：第 1 周 … 第 N 周，每个事件都是全天，
# 这样在日历里能一眼看到"这周是第几周"。
#
# 学期信息（第 1 周周一 + 总周数）优先从 main 程序写下的 res\semester.json 读取，
# 也可以手动指定 -Start / -Weeks；不需要登录、不需要联网。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File .\weeks.ps1
#   powershell -ExecutionPolicy Bypass -File .\weeks.ps1 -Start 2026-09-14 -Weeks 17
#   powershell -ExecutionPolicy Bypass -File .\weeks.ps1 -Mode day      # 每天一条
#   powershell -ExecutionPolicy Bypass -File .\weeks.ps1 -Out res\weeks.ics -Term "2026-2027学年秋季学期"
#
# -Start <yyyy-MM-dd>  第 1 周的周一
# -Weeks <N>           总周数
# -Term <名称>         学期名（只影响日历名与描述）
# -Out <文件>          输出路径，默认 res\weeks.ics
# -Mode week|day       week = 每周一条 7 天的事件（默认）；day = 每天一条
# -Quiet               只输出结果，不打印提示

param(
    [string] $Start,
    [int] $Weeks = 0,
    [string] $Term = '',
    [string] $Out = '',
    [ValidateSet('week', 'day')][string] $Mode = 'week',
    [switch] $Quiet
)

$ErrorActionPreference = 'Stop'
$RepoRoot = $PSScriptRoot

function Fail([string] $Message) {
    Write-Host "❌ $Message" -ForegroundColor Red
    exit 1
}

# ---------- 读取学期信息 ----------
function Read-SemesterInfo {
    $candidates = @(
        (Join-Path $RepoRoot 'res\semester.json'),
        (Join-Path $RepoRoot 'semester.json')
    )
    foreach ($path in $candidates) {
        if (-not (Test-Path -LiteralPath $path)) { continue }
        try {
            $json = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
        }
        catch { continue }
        if ($json.firstDay -and [int]$json.weekCount -gt 0) {
            return [pscustomobject]@{
                FirstDay = [string]$json.firstDay
                WeekCount = [int]$json.weekCount
                SemesterName = [string]$json.semesterName
                Source = $path
            }
        }
    }
    return $null
}

$info = Read-SemesterInfo

if (-not $Start) {
    if (-not $info) {
        Fail "找不到学期信息：请先跑一次 run.cmd（会写 res\semester.json），或用 -Start 2026-09-14 -Weeks 17 手动指定。"
    }
    $Start = $info.FirstDay
}
if ($Weeks -le 0) {
    if ($info -and $info.WeekCount -gt 0) { $Weeks = $info.WeekCount }
    else { Fail "缺少周数：请用 -Weeks 指定（例如 -Weeks 17）。" }
}
if (-not $Term -and $info) { $Term = $info.SemesterName }
if (-not $Out) { $Out = Join-Path $RepoRoot 'res\weeks.ics' }
elseif (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $RepoRoot $Out }

$startDate = [datetime]::MinValue
try {
    $startDate = [datetime]::ParseExact($Start, 'yyyy-MM-dd', [System.Globalization.CultureInfo]::InvariantCulture)
}
catch {
    Fail "日期格式不对：$Start（应为 yyyy-MM-dd，例如 2026-09-14）。"
}
if ($startDate.DayOfWeek -ne [System.DayOfWeek]::Monday) {
    $names = @('周日', '周一', '周二', '周三', '周四', '周五', '周六')
    Fail "$Start 不是周一（它是$($names[[int]$startDate.DayOfWeek])）。第 1 周的周一请填周一。"
}
# 极端情况下 date arithmetic 会因时区/夏令时抖动，统一按日期部分处理。
$startDate = $startDate.Date
$endDate = $startDate.AddDays($Weeks * 7)

# ---------- iCalendar 基础 ----------
function ConvertTo-IcsText([string] $Text) {
    if ($null -eq $Text) { return '' }
    $value = $Text -replace '\\', '\\\\'
    $value = $value -replace ';', '\;'
    $value = $value -replace ',', '\,'
    $value = $value -replace "`r`n", '\n'
    $value = $value -replace "`n", '\n'
    $value = $value -replace "`r", '\n'
    return $value
}

# 按 75 字节折行（UTF-8 八位组），续行以空格开头；不能把多字节字符切断。
function Format-IcsLine([string] $Line) {
    $encoding = [System.Text.Encoding]::UTF8
    $bytes = $encoding.GetBytes($Line)
    if ($bytes.Length -le 75) { return $Line }

    $parts = New-Object System.Collections.Generic.List[string]
    $offset = 0
    while ($offset -lt $bytes.Length) {
        $budget = if ($parts.Count -eq 0) { 75 } else { 74 }
        $remaining = $bytes.Length - $offset
        if ($remaining -le $budget) {
            $parts.Add($encoding.GetString($bytes, $offset, $remaining))
            break
        }
        $length = $budget
        while ($length -gt 1 -and ($bytes[$offset + $length] -band 0xC0) -eq 0x80) { $length-- }
        $parts.Add($encoding.GetString($bytes, $offset, $length))
        $offset += $length
    }
    return ($parts -join "`r`n ")
}

function Format-IcsDate([datetime] $Date) { return $Date.ToString('yyyyMMdd') }

$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$weekdayNames = @('周日', '周一', '周二', '周三', '周四', '周五', '周六')
$calendarName = if ($Term) { "清华大学课表（$Term 周次）" } else { '清华大学课表（周次）' }
$uidDomain = '@thu-courses-ics'
$uidPrefix = 'thu-week-' + (Format-IcsDate $startDate)

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('BEGIN:VCALENDAR')
$lines.Add('VERSION:2.0')
$lines.Add('PRODID:-//thu-courses-ics//weeks//CN')
$lines.Add('CALSCALE:GREGORIAN')
$lines.Add('METHOD:PUBLISH')
$lines.Add((Format-IcsLine ('X-WR-CALNAME:' + (ConvertTo-IcsText $calendarName))))

function Add-AllDayEvent {
    param([datetime] $From, [datetime] $To, [string] $UidSuffix, [string] $Summary, [string] $Description)

    $lines.Add('BEGIN:VEVENT')
    $lines.Add((Format-IcsLine ('UID:' + $uidPrefix + '-' + $UidSuffix + $uidDomain)))
    $lines.Add('DTSTAMP:' + $stamp)
    $lines.Add('DTSTART;VALUE=DATE:' + (Format-IcsDate $From))
    $lines.Add('DTEND;VALUE=DATE:' + (Format-IcsDate $To))
    $lines.Add((Format-IcsLine ('SUMMARY:' + (ConvertTo-IcsText $Summary))))
    if ($Description) {
        $lines.Add((Format-IcsLine ('DESCRIPTION:' + (ConvertTo-IcsText $Description))))
    }
    $lines.Add('TRANSP:TRANSPARENT')
    $lines.Add('END:VEVENT')
}

for ($week = 1; $week -le $Weeks; $week++) {
    $weekStart = $startDate.AddDays(($week - 1) * 7)
    $weekEnd = $weekStart.AddDays(7)
    $range = '{0} ~ {1}' -f $weekStart.ToString('yyyy-MM-dd'), $weekEnd.AddDays(-1).ToString('yyyy-MM-dd')
    $termText = if ($Term) { $Term } else { '学期' }

    if ($Mode -eq 'week') {
        Add-AllDayEvent -From $weekStart -To $weekEnd -UidSuffix ('w' + $week) `
            -Summary ('第 ' + $week + ' 周') `
            -Description ("$termText 第 $week 周（$range）")
    }
    else {
        for ($day = 0; $day -lt 7; $day++) {
            $date = $weekStart.AddDays($day)
            $name = $weekdayNames[[int]$date.DayOfWeek]
            Add-AllDayEvent -From $date -To $date.AddDays(1) -UidSuffix ('w' + $week + 'd' + ($day + 1)) `
                -Summary ('第 ' + $week + ' 周 · ' + $name) `
                -Description ("$termText 第 $week 周 $name（" + $date.ToString('yyyy-MM-dd') + '）')
        }
    }
}
$lines.Add('END:VCALENDAR')

$directory = Split-Path -Parent $Out
if ($directory -and -not (Test-Path -LiteralPath $directory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
$text = ($lines -join "`r`n") + "`r`n"
[System.IO.File]::WriteAllText($Out, $text, (New-Object System.Text.UTF8Encoding($false)))

if (-not $Quiet) {
    $eventCount = if ($Mode -eq 'week') { $Weeks } else { $Weeks * 7 }
    Write-Host ""
    Write-Host "✅ 已生成 $Out" -ForegroundColor Green
    Write-Host ("   学期：{0}" -f $(if ($Term) { $Term } else { '（未指定名称）' }))
    Write-Host ("   第 1 周周一：{0}    共 {1} 周（{2} ~ {3}）" -f $startDate.ToString('yyyy-MM-dd'), $Weeks,
                $startDate.ToString('yyyy-MM-dd'), $endDate.AddDays(-1).ToString('yyyy-MM-dd'))
    Write-Host ("   事件：{0} 条（{1}）" -f $eventCount, $(if ($Mode -eq 'week') { '每周一条，覆盖 7 天' } else { '每天一条' }))
    if ($info) { Write-Host ("   学期信息来源：{0}" -f $info.Source) -ForegroundColor DarkGray }
    Write-Host ""
    Write-Host "把这文件导入日历 App 即可；想每天都能看到周次就用 -Mode day。" -ForegroundColor Gray
}
exit 0
