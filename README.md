# thu-courses-ics（C++ 版）

自动登录清华大学信息门户 / 教学门户，读取本学期（或指定下学期）的课表，
生成可供日历 App 订阅的 iCalendar 文件。每次按学号写一个文件，默认输出到：

```
res/<代号>.ics
```

既支持单人使用，也支持多学生批量生成（见[多学生 / 批量生成](#多学生--批量生成)）。
本仓库是独立项目，只负责「登录 → 读取课表 → 生成 .ics」。发布到 GitHub Pages
之类的静态站点仓库需要额外一步复制文件，见[发布到网站](#发布到网站)。

实现参考了 [`thu-info-community/thu-info-lib`](https://github.com/thu-info-community/thu-info-lib)
与 [`thu-info-community/thu-info-app`](https://github.com/thu-info-community/thu-info-app)
的登录、WebVPN 漫游和课表解析逻辑（对应 `@thu-info/lib@3.15.2` 的
`src/lib/core.ts`、`src/lib/basics.ts`、`src/lib/schedule.ts`、
`src/models/schedule/schedule.ts`、`src/constants/strings.ts`）。

## 特点

- **零第三方依赖**：HTTP(S) 使用 Windows 自带的 WinHTTP，SM2 / SM3 /
  JSON 解析全部内置；编译后是单个 `.exe`。
- **完整登录链路**：id.tsinghua.edu.cn 的 SM2 密码加密、两步验证时在终端
  **手动输入 6 位 TOTP 验证码**、信任本机指纹、WebVPN 应用漫游。
- **课表还原**：教学日历（`jxrl`）为主，补充本科“二级课表”，按
  `第 N 周 + 星期 X + 第 a–b 节` 展开成具体日期时间，包含考试。
- **可离线复现**：`--dump-dir` 保存原始接口数据，`--offline` 可在不登录的
  情况下重新生成 ICS，便于排查问题。
- **标准 iCalendar**：UTC 时间、UTF-8、75 字节折行、稳定的 UID，兼容
  Google Calendar / Apple Calendar / Outlook。
- **双击即用**：仓库根目录的 `run.cmd` 双击运行：首次自动编译、缺 `.env` 时打开
  记事本让你填写、问你复制到哪个站点仓库；复制完自动提交并推送，窗口不会自动关闭。

## 快速开始（双击运行）

1. 安装 [CMake](https://cmake.org/download/) ≥ 3.20 与
   [MinGW-w64](https://winlibs.com/)（或 MSVC），确保 `cmake` / `g++` 在 `PATH` 里。
2. 双击仓库根目录的 `run.cmd`。
3. 首次运行会：
   - 从 `.env.example` 生成 `.env` 并打开记事本，填好学号与密码后保存、回到窗口按回车；
   - 自动编译出 `build\thu-courses-ics.exe`（或 `build\Release\` 下的同名文件）。
4. 登录时如果账号开了两步验证，按提示在窗口里输入 6 位 TOTP 验证码；随后会问
   **是否在清华 ID 内信任本设备**：选 `y` 以后基本不用再输验证码，选 `n` 或回车
   则每次登录都要输。详见[配置凭据](#配置凭据)。
5. 生成后脚本会问「生成的 ics 要复制到站点仓库吗？」，四种回答：
   - **`default`（或 `d`）**：用脚本检测到的同级 `<名字>.github.io`，同时把该路径写进
     `.env` 的 `THU_PUBLISH_DIR`。**以后运行不再询问**，直接照此导出；
   - **`once`（或 `o`）**：只在本次使用检测到的同级目录，不写入 `.env`；
   - **目录路径**：本次导出到该目录，不写入 `.env`；
   - **留空或 `n`**：不导出。
6. 复制完会自动在站点仓库里 `git add` → `git commit` → `git push`，成功后打印提交号。
7. 结束时窗口停在 `Press any key to continue . . .`，按任意键关闭；
   本地文件在 `res\<代号>.ics`。

> 想改已记住的目标：编辑 `.env` 里的 `THU_PUBLISH_DIR`，或临时用
> `-Publish <目录>` 覆盖；命令行加 `-NoPush` 则只复制不提交。
>
> 只想在命令行里跑（不弹交互、适合任务计划）：用
> `powershell -ExecutionPolicy Bypass -File .\run.ps1 -NonInteractive`，
> 它会读取 `.env` 里的目标并静默完成复制与推送。

## 构建

要求：

- Windows 10/11 x64
- CMake ≥ 3.20
- MinGW-w64 g++（推荐，已在 GCC 16.2 上验证）或 MSVC（已在 VS 17 2022 上验证）

在仓库根目录执行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

产物：

```
build/thu-courses-ics.exe          # MinGW / Ninja 等单配置生成器
build/Release/thu-courses-ics.exe  # Visual Studio 等多配置生成器
```

也可以直接运行一键脚本（等价于上面两条命令）：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

`build.ps1` 如果发现 `build\CMakeCache.txt` 已经存在，会**沿用原来的生成器**
（例如你之前在 Visual Studio 里配置过），不会因为生成器不一致而报错；`run.cmd`
也会自动在 `build\`、`build\Release\`、`build\Debug\` 里挑最新的那个 exe。

先跑一遍内置测试（不联网、不需要账号）：

```powershell
.\build\Release\thu-courses-ics.exe --self-test
```

测试覆盖 SM2 标准向量与加解密、SM3、JSON、日期换算、学期与课表解析、
ICS 生成与折行、两步验证的设备信任选项解析、TOTP 有效期窗口计算，以及批量
账号列表解析。

## 配置凭据

复制模板并填写：

```powershell
copy .\.env.example .\.env
notepad .\.env
```

`.env` 已被 `.gitignore` 忽略，不会被提交。程序会依次查找：

1. 当前工作目录的 `.env`（推荐从仓库根目录运行）
2. 可执行文件同目录、上一级、上两级目录的 `.env`

| 变量 | 必填 | 说明 |
| --- | --- | --- |
| `THU_USER_ID` | 是* | 学号（单个学生） |
| `THU_PASSWORD` | 是* | 信息门户密码 |
| `THU_ACCOUNTS` | 否 | 账号列表文件路径，用于批量生成（见[多学生 / 批量生成](#多学生--批量生成)） |
| `THU_SEMESTER_INDEX` | 否 | 抓取下学期：`nextSemesterList` 下标，0 起；留空为当前学期 |
| `THU_OUTDIR` | 否 | 输出目录，默认 `res`；文件名为 `<代号>.ics` |
| `THU_OUTPUT` | 否 | 强制指定单个输出文件（仅单账号，留空则用 `<outdir>/<代号>.ics`） |
| `THU_ID_SALT` | 否 | 学号 → 代号的盐；留空则用本地 `.id-salt`（没有就自动生成） |
| `THU_CALNAME` | 否 | 日历名称 |
| `THU_FINGERPRINT` | 否 | 设备指纹；默认按“学号 + 计算机名”生成并保持稳定（批量模式忽略） |
| `THU_TRUST_DEVICE` | 否 | 两步验证时是否在清华 ID 内信任本设备：留空=登录时询问一次；`1`=总是信任；`0`=从不信任 |
| `HTTPS_PROXY` | 否 | HTTP 代理，如 `http://127.0.0.1:15236`；默认直连 |

\* `THU_USER_ID` / `THU_PASSWORD` 与 `THU_ACCOUNTS` 至少要提供一种。

也可以使用同名命令行参数覆盖（见 `--help`）。**密码建议只写在 `.env`**，
避免出现在命令历史中。

> 如果账号启用了 TOTP 两步验证，登录时程序会暂停并提示：
> `请输入身份验证器 App 中的 6 位 TOTP 验证码`。输入当前 6 位数字即可，
> 不需要把密钥保存到任何文件；输入 `q` 可取消。
>
> 验证码通过后会在服务端当前 30 秒窗口内被记住：同一次运行里如果因为 WebVPN
> 会话失效而再次触发两步验证，只要还在这个窗口内就直接复用、不再询问；
> 若服务端判定它已失效，则提示「刚才的验证码已失效，请重新输入」并要求重输。
>
> 验证码通过后还会问一次**是否在清华 ID 内信任本设备**（`[y/N]`，默认不信任）：
> - 选 `y`：调用 `b/doubleAuth/personal/saveFinger` 登记本机指纹，以后登录通常
>   不再要求验证码；想撤销可到 id.tsinghua.edu.cn 的设备管理里删除；
> - 选 `n` 或直接回车：不登记，**下次登录（哪怕第二天）仍会要求输入 TOTP 验证码**。
>
> 不想每次都被问，就用 `--trust-device` / `--no-trust-device` 或
> `.env` 的 `THU_TRUST_DEVICE=1|0` 一次性定好。

> 清华的登录入口只对国内网络开放；如果你使用代理，请让
> `*.tsinghua.edu.cn` 直连。GitHub Actions 等海外 runner 无法完成登录，
> 因此本工具设计为在本机运行。

## 生成

在仓库根目录执行（默认输出 `res\<代号>.ics`）：

```powershell
.\build\thu-courses-ics.exe
```

或使用包装脚本（会自动切换工作目录、首次运行时构建、缺 `.env` 时引导填写）：

```powershell
powershell -ExecutionPolicy Bypass -File .\run.ps1
```

最省事的是**直接双击 `run.cmd`**，效果同上，且结束后窗口会停住方便看输出
（见[快速开始](#快速开始双击运行)）。

示例输出：

```
🔐 正在登录清华大学信息门户（2022xxxxxx）…
📚 正在读取学期信息…
📅 正在读取课表：2026-2027学年秋季学期…
✅ 已生成 C:\...\thu-ics-cpp\res\3f2a9c41d7e05b68.ics
   学期：2026-2027学年秋季学期（第 1 周周一 = 2026-09-14，共 17 周）
   课程：23 门，事件：412 个
```

文件名不是学号，而是**由学号派生出的 16 位十六进制代号**（见下节）。可以用
`--outdir <目录>` 换输出目录，或用 `--output <文件>` 强行指定单个文件
（仅单账号可用）。

### 代号（文件名）是怎么来的

```
代号 = SM3(盐 + \x1f + 学号) 的前 16 位十六进制
```

- **确定且唯一**：同一个盐下，同一学号永远得到同一个代号，不同学号不会撞车
  （64 位，几千人规模下碰撞概率可忽略）；
- **不可反查**：盐只保存在本地 `.id-salt`（首次运行自动生成，32 位十六进制），
  只有拿到这个文件的人才能把代号对回学号；代号的 URL 可以放心公开；
- 盐的来源优先级：`--id-salt` → 环境变量 / `.env` 的 `THU_ID_SALT` → 本地
  `.id-salt` → 自动生成；
- 程序会在生成时打印一行 `🆔 学号 … → 代号 …`，方便你自己对照；
- ⚠️ **`.id-salt` 不要删除、不要公开**：删掉后会重新生成新的盐，所有代号都会变，
  站点上会出现一批新的文件名，旧文件仍在。

### 对照表（`res/id-map.txt`）

每次生成完会写一份本地对照表，避免日后忘了哪个文件是谁的：

```text
# thu-courses-ics 学号-代号对照表
# 含学号：仅留在本地，不要提交、不要随站点发布。
# 代号	学号
c8e635928c2a7ada	2026010585
3f2a9c41d7e05b68	2026010586
```

- 默认写在 `<outdir>/id-map.txt`，可以用 `--id-map <文件>` 换位置；
- 与已有文件**合并**：同一学号以本次为准，其它历史记录保留，所以分批跑多个学生
  不会互相覆盖；本次失败的账号不会写进去；
- 文件含学号，属于本地数据：`res/` 本来就被 `.gitignore` 忽略，`run.ps1` 也只会
  复制 `*.ics`，所以它不会跟着发布出去。

## 多学生 / 批量生成

把多个学生的账号写进一个列表文件（建议叫 `accounts.txt`，放在仓库根目录，
已被 `.gitignore` 忽略）：

```text
# 每行「学号,密码」；分隔符可以用 , ; : = | / 或空格
2026010585,我的密码
2026010586=另一个密码
# 密码里带 # 或 , 也没问题，只按学号后的第一个分隔符切开
2026010587; p@ss,word
```

然后：

```powershell
# 批量生成到 res\<代号>.ics
.\build\thu-courses-ics.exe --accounts accounts.txt

# 或写进 .env，之后双击 run.cmd 就会全部生成
# THU_ACCOUNTS=accounts.txt

# 指定输出目录
.\build\thu-courses-ics.exe --accounts accounts.txt --outdir res\2026-2027-1
```

行为说明：

- 单人凭据（`THU_USER_ID` / `THU_PASSWORD`）和 `--accounts` **可以同时给**，
  单账号排在前面，按学号去重；
- 每个学号使用**各自的设备指纹**（由学号 + 计算机名生成），不会让多个人共用同一台
  “可信设备”；批量模式下 `--fingerprint` 被忽略；
- `--dump-dir` 在批量模式会按学号分子目录：`<dump-dir>\<学号>\`；
- 某个账号失败（密码错、未开 TOTP 等）**不会中断整批**，其余账号继续生成，
  最后汇总 `❌ n/m 个账号失败` 并以退出码 1 结束；
- 多账号时不能用 `--output`（会直接报错），请用 `--outdir`；
- 每个账号可能各自触发两步验证（除非该设备已被信任），验证码询问是逐个账号进行的。

## 发布到网站

`res/` 被本仓库的 `.gitignore` 忽略（课表属于个人数据，不适合放在公开的代码仓库
历史里）。发布动作由脚本完成：复制文件到你指定的站点仓库，然后在该仓库里
`git add` → `git commit` → `git push`。识别与推送逻辑放在 `publish.ps1`，
`run.ps1` 和 `weeks.ps1` 共用同一套。

目标目录的解析顺序（先命中先用）：

1. 命令行 `-Publish <目录>`
2. 环境变量 `THU_PUBLISH_DIR`
3. `.env` 里的 `THU_PUBLISH_DIR`——回答过 `default` 后就会写在这里
4. 交互询问（见下）

```powershell
# 指定目标：复制 + 自动提交 + 自动推送
powershell -ExecutionPolicy Bypass -File .\run.ps1 -Publish ..\your-username.github.io

# 只复制，不提交也不推送
powershell -ExecutionPolicy Bypass -File .\run.ps1 -Publish ..\your-username.github.io -NoPush

# 之后就可以无参数运行，脚本会沿用 .env 里的目标
powershell -ExecutionPolicy Bypass -File .\run.ps1
```

双击 `run.cmd` 且 `.env` 里还没有目标时，会问「生成的 ics 要复制到站点仓库吗？」：

| 回答 | 行为 |
| --- | --- |
| `default` / `d` | 用检测到的同级 `<名字>.github.io`，并把该路径写入 `.env` 的 `THU_PUBLISH_DIR`；**下次不再询问** |
| `once` / `o` | 只在本次使用检测到的同级目录，不写入 `.env` |
| 目录路径 | 本次导出到该目录，不写入 `.env` |
| 留空 / `n` / `否` / `skip` | 不导出 |

`.env` 里已有 `THU_PUBLISH_DIR` 时完全不再提问，只打印一行「导出目标（来自 .env）」。
想改就编辑 `.env` 那一行，或临时用 `-Publish` 覆盖。

自动推送的行为细节：

- 只 `git add` 本次复制的那几个 `<学号>.ics`，并 `git commit -- res/<代号>.ics …`，
  站点仓库里其它已暂存的改动不会被卷进这次提交；
- 批量生成时一次复制 / 提交全部文件（有几个学号就是几个）；
- 内容与上次完全一致时跳过提交，不会产生空提交；
- 分支没有上游时自动改用 `git push -u origin <当前分支>`；
- 目标目录不是 git 仓库时只复制、不提交（给出提示）；
- 推送失败（凭据失效、远程有新提交等）会以退出码 1 结束，文件已提交在本地，
  你可以在站点仓库里手动处理后再 `git push`。

订阅地址为（每个学号一个）：

```
https://your-username.github.io/res/<代号>.ics
webcal://your-username.github.io/res/<代号>.ics
```

如果确认新文件正确，可以把站点首页（如 `index.html` / `index.md`）里的订阅链接
从 `res/courses.ics` 切换到 `res/<代号>.ics`。

> 发布出去的是代号文件名，不含学号；但课表内容本身（课程、教室）仍然是个人数据，
> 只要 URL 公开，拿到链接的人都能看到课表。介意的话就别发布，或放到私有仓库。

### 推送后自动打印订阅链接

推送成功后，`run.ps1` 会直接列出每个文件的订阅地址，并把代号翻译回学号
（读 `res/id-map.txt`）：

```
📅 订阅链接：
  res/c8e635928c2a7ada.ics    （学号 2026010585）
    webcal://your-username.github.io/res/c8e635928c2a7ada.ics
    https://your-username.github.io/res/c8e635928c2a7ada.ics
```

站点根地址的解析顺序：

1. 命令行 `-PublishUrl https://example.com/`
2. `.env` 里的 `THU_PUBLISH_URL`
3. 站点仓库根目录的 `CNAME`（自定义域名）
4. 从站点仓库的 `git remote origin` 推导：`https://github.com/<user>/<user>.github.io`
   → `https://<user>.github.io/`；项目仓库（`<user>/<repo>`）→
   `https://<user>.github.io/<repo>/`

四者都没有时只打印相对路径，并提示用 `-PublishUrl` 指定。`webcal://` 那行可以直接
点开，让日历 App 订阅。

## 周次日历（第 N 周全天事件）

另一个独立的小工具 `weeks.ps1` / `weeks.cmd`，按学期校历生成一份**全是全天事件**的
日历，标明第几周，方便在日历里一眼看出"这周是第几周"。

不需要登录、不需要联网：主程序每次生成课表时会把学期信息写到
`res/semester.json`，`weeks.cmd` 直接读它：

```json
{
  "semesterName": "2026-2027学年秋季学期",
  "firstDay": "2026-09-14",
  "weekCount": 17
}
```

用法：

```powershell
# 双击 weeks.cmd，或：
powershell -ExecutionPolicy Bypass -File .\weeks.ps1

# 每周一条（默认是每天一条）
powershell -ExecutionPolicy Bypass -File .\weeks.ps1 -Mode week

# 手动指定学期（没有 semester.json 时）
powershell -ExecutionPolicy Bypass -File .\weeks.ps1 -Start 2026-09-14 -Weeks 17

# 生成后直接发布到站点仓库（目录识别与 run.ps1 完全相同）
powershell -ExecutionPolicy Bypass -File .\weeks.ps1 -Publish ..\your-username.github.io

# 换输出位置 / 学期名
powershell -ExecutionPolicy Bypass -File .\weeks.ps1 -Out res\weeks.ics -Term 2026-2027学年秋季学期
```

输出（默认 `res\weeks.ics`，纯全天事件、`TRANSP:TRANSPARENT` 不占忙碌时间）：

```
BEGIN:VEVENT
UID:thu-week-20260914-w3d3@thu-courses-ics
DTSTAMP:20260914T000000Z
DTSTART;VALUE=DATE:20260930
DTEND;VALUE=DATE:20261001
SUMMARY:第 3 周 · 周三
DESCRIPTION:2026-2027学年秋季学期 第 3 周 周三（2026-09-30）
TRANSP:TRANSPARENT
END:VEVENT
```

| 模式 | 事件数 | 说明 |
| --- | --- | --- |
| `-Mode day`（默认） | N × 7 条 | 每天一条，标题形如 `第 3 周 · 周三`，任何一天都能看到周次 |
| `-Mode week` | 每学期 N 条 | 一条事件覆盖周一~周日，日历里整周都显示「第 N 周」 |

`-Start` 必须是周一（否则直接报错），`-Weeks` 省略时用 `semester.json` 里的周数。
输出同样是 UTF-8、CRLF、75 字节折行，和主程序的 `.ics` 规范一致；`DTSTAMP` 固定取
学期首日，所以同样的输入每次生成完全相同的文件——重复发布不会产生空提交。

### 发布周次日历

`weeks.ps1` 与 `run.ps1` 共用 `publish.ps1`，所以发布规则完全一样：

- 目标解析顺序同样是 `-Publish` → `THU_PUBLISH_DIR` → `.env` → 交互询问
  （`default` / `once` / 路径 / 留空），回答过 `default` 后两个工具都记住同一处；
- 支持 `-PublishUrl`（站点根地址）、`-NoPush`（只复制）、`-NonInteractive`；
- 推送成功后同样打印 `webcal://…` 与 `https://…` 订阅链接。

```powershell
# 生成 + 复制 + 提交 + 推送 + 打印链接
powershell -ExecutionPolicy Bypass -File .\weeks.ps1 -Publish ..\your-username.github.io

# 只生成不发布
powershell -ExecutionPolicy Bypass -File .\weeks.ps1 -NonInteractive
```

> `weeks.ics` 不含个人信息，可以放心公开（这也是它适合订阅分享的原因）。

## 可选参数

上面这些是 `thu-courses-ics.exe` 自己的参数，写在 `run.ps1` / `run.cmd` 后面会原样
透传：

```
--user-id <学号>         --password <密码>
--accounts <文件>        --outdir <目录>
--output <文件>          --id-salt <文本>
--semester <N>           --calname <名称>
--proxy <host:port>      --fingerprint <hex>
--dump-dir <目录>        --trust-device
--no-trust-device        --offline <目录>
--verbose                --self-test
```

`--trust-device` / `--no-trust-device` 决定两步验证后是否在清华 ID 内登记本设备；
两个都不写时，触发两步验证后会问一次（对应 `.env` 的 `THU_TRUST_DEVICE`）。

`run.ps1` / `weeks.ps1` 另外提供这些开关（两者共用）：

```
-Publish <目录>          复制到该站点仓库的 res\（并记住）
-PublishUrl <url>        打印订阅链接时用的站点根地址
-NoPush                  只复制，不 git add/commit/push
-NonInteractive          不询问任何问题（任务计划 / CI 用）
```

`weeks.ps1` 自己的参数还有 `-Start` / `-Weeks` / `-Term` / `-Out` / `-Mode` / `-Quiet`，
见[周次日历](#周次日历第-n-周全天事件)。

### 保存原始数据 / 离线重新生成

```powershell
.\build\thu-courses-ics.exe --dump-dir .\dump --verbose
```

会得到：

```
calendar.json    # 学期列表接口原文
primary.json     # jxrl 课表 JSON 数组
secondary.html   # 本科二级课表页面
```

之后可在不联网、不登录的情况下重新生成：

```powershell
.\build\thu-courses-ics.exe --offline .\dump --output res\coursesAuto.ics
```

> `dump` 目录包含你的真实课表，已被 `.gitignore` 忽略，注意不要强行提交。

## 定时运行（可选）

`run.ps1` 会先切换到仓库根目录，因此可以直接交给任务计划程序：

```powershell
schtasks /Create /SC DAILY /TN "THU Courses ICS" /ST 06:00 /TR "powershell -ExecutionPolicy Bypass -File C:\path\to\thu-ics-cpp\run.ps1 -NonInteractive -Publish C:\path\to\your-username.github.io"
```

`-NonInteractive` 关闭所有提示（无控制台输入时脚本本来也会自动跳过询问）。
省略 `-Publish` 也可以：先双击 `run.cmd` 回答一次 `default`，把目标写进 `.env` 的
`THU_PUBLISH_DIR`，之后定时任务只写 `-NonInteractive` 就会沿用该目标，并把提交
直接推送上去。

删除任务：

```powershell
schtasks /Delete /TN "THU Courses ICS" /F
```

> 无人值守的推送依赖站点仓库已保存好的凭据（Windows 凭据管理器或 SSH key）。
> 如果推送失败，文件仍会生成并提交在站点仓库的本地，只是没有推上去。

## 实现说明

### 登录

1. `GET https://webvpn.tsinghua.edu.cn/login?oauth_login=true`，跟随跳转；
2. 从 id.tsinghua.edu.cn 登录页读取 `#sm2publicKey`；
3. 用 SM2（C1C3C2，`04||C1||C3||C2`）加密密码，POST 到
   `/do/off/ui/auth/login/check`：
   `i_user` / `i_pass` / `fingerPrint` / `fingerGenPrint` / `i_captcha`；
4. 若返回“二次认证”，走 `b/doubleAuth/login` 的
   `FIND_APPROACHES` → `SEND_CODE(type=totp)`，然后提示你在终端**手动输入
   6 位 TOTP 验证码**，再调用 `VERITY_TOTP_CODE`；通过后就记住这个验证码及其
   30 秒窗口的结束时刻，窗口内再次触发两步验证时先直接复用（服务端拒绝则清空
   并重新询问）；通过后按 `THU_TRUST_DEVICE` / `--trust-device` /
   `--no-trust-device` 的决定（没配置时当场询问）调用
   `b/doubleAuth/personal/saveFinger` 登记本机指纹；
5. 从成功页取回调链接，建立 WebVPN 会话，再执行一次
   `form/10000ea...` 的 ID 登录和 `oauth.tsinghua.edu.cn/lb-auth/lbredirect`
   跳转。

验证码不会被保存到磁盘，也不会出现在日志中；如果连续输错，会提示重新输入。
复用的范围仅限本次进程（窗口结束即作废），下次运行仍会重新询问。

SM2 使用内置的 256 位 Montgomery 模运算 + Jacobian 坐标实现，SM3 按
GB/T 32918 实现；已用 `sm-crypto` 生成的密文做双向交叉验证。

### 学期与课表

- `LEARN_HOME_URL` 中取 `_csrf`，再请求
  `getCurrentAndNextSemester`，得到 `kssj/jssj/id/xnxqmc`；
- 第 1 周周一 = `kssj + 2 天`（接口返回的通常是周六），
  周数 = `(jssj - kssj) / 7 + 1`；
- 教学日历分页请求：
  `jxmh_out.do?m=bks_jxrl_all&p_start_date=...&p_end_date=...&jsoncallback=m`，
  每 3 周一组，拼接成 JSON 数组；
- 本科额外请求 `portal3rd.do?m=bks_ejkbSearch`，解析其中的二级课表脚本；
- 若接口因 WebVPN 会话失效而返回登录页，会先通过
  `wengine-vpn/cookie` + `onlineAppRedirect` 做应用漫游，必要时重新登录。

### ICS

- 日期 = `第 1 周周一 + (week-1)*7 + (dayOfWeek-1)`；
- 时间按教务处节次表换算为北京时间，再转 UTC（`...Z`）；
- 每门课每个时间片每周一个 `VEVENT`，考试单独成事件；
- `UID` 由“课程名 + 地点”的 FNV-1a 哈希生成，多次运行保持稳定。

### 节次时间表

| 节次 | 时间 | 节次 | 时间 |
| --- | --- | --- | --- |
| 1 | 08:00–08:45 | 8 | 15:20–16:05 |
| 2 | 08:50–09:35 | 9 | 16:10–16:55 |
| 3 | 09:50–10:35 | 10 | 17:05–17:50 |
| 4 | 10:40–11:25 | 11 | 17:55–18:40 |
| 5 | 11:30–12:15 | 12 | 19:20–20:05 |
| 6 | 13:30–14:15 | 13 | 20:10–20:55 |
| 7 | 14:20–15:05 | 14 | 21:00–21:45 |

## 限制与排错

- **仅 Windows**：WinHTTP / BCrypt / SChannel 是 Windows API。
- **SChannel 受限环境**：如果在沙箱、服务账号或受限令牌中运行，可能报
  `WinHTTP error (code 12185)`。这是进程无法访问 Windows SChannel 凭据，
  请在普通桌面终端中运行。
- **需要国内网络**：登录服务对海外网络不开放；请让清华域名直连。
- **两步验证**：登录时手动输入 TOTP 验证码；微信/短信验证无法自动完成。
  填完验证码后可以选择是否信任本设备：信任后本机后续登录通常不再触发 2FA，
  无人值守的定时任务才有可能跑通；不信任则每次都会等标准输入，定时任务会报错。
- **二级课表**：页面格式没有公开接口，解析采用宽松匹配；失败时只警告并
  继续使用教学日历，不影响主课表。
- **自定义日程**：`getSchedule()` 不包含 App 本地添加的自定义日程，
  因此本工具也不会生成它们。
- **密码错误 / 需要验证**：错误信息会从登录页的 `msg_note` 提取；若显示
  “用户名或密码不正确”，请检查 `.env`。
- **数据隐私**：生成的 `.ics` 一旦发布就会公开托管，任何拿到 URL 的人都能看到
  课表（文件名是代号、不含学号，但课表内容是真实的）。本仓库已在 `.gitignore`
  中忽略 `res/`、`dump/`、`accounts.txt` 和 `.id-salt`，如不接受公开请不要发布。
- **`.id-salt` 要保密**：它就是学号 ↔ 代号的钥匙，泄露后别人可以暴力比对
  （学号空间只有十位数）。不要提交、不要发到群里。
- **批量生成的凭据**：`accounts.txt` 里是明文密码，务必只放在本机、不要提交、
  不要放进共享目录；可以把 `--accounts` 指向仓库外的路径。

## 目录

```
thu-ics-cpp/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── .gitattributes
├── .env.example
├── .id-salt            # 学号 → 代号的盐（首次运行生成，已忽略，务必保密）
├── accounts.txt        # 批量生成的账号列表（本地创建，已忽略）
├── run.cmd             # 双击运行入口（纯 ASCII 启动器）
├── run.ps1             # 构建检查、.env 引导、运行、复制到站点仓库
├── weeks.cmd           # 双击生成「第 N 周」全天事件日历
├── weeks.ps1           # 读 semester.json → 生成 res\weeks.ics
├── publish.ps1         # 共用：站点目录识别、复制、git 提交推送、订阅链接
├── build.ps1           # 只负责 cmake 配置 + 编译
├── src/
│   ├── main.cpp        # CLI、配置、流程编排
│   ├── thu.cpp/.h      # 登录、2FA、WebVPN 漫游、原始接口
│   ├── schedule.cpp/.h # 学期/课表解析、节次表
│   ├── ics.cpp/.h      # 事件展开与 iCalendar 序列化
│   ├── http.cpp/.h     # WinHTTP 客户端（Cookie、重定向、代理）
│   ├── sm2.cpp/.h      # SM2（含 256 位模运算）
│   ├── sm3.cpp/.h      # SM3
│   ├── json.cpp/.h     # 极简 JSON 解析
│   ├── util.cpp/.h     # 字符串、日期、编码、折行
│   └── selftest.cpp/.h
└── tests/tests.cpp
```
