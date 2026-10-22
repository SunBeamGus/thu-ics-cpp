# thu-courses-ics（C++ 版）

自动登录清华大学信息门户 / 教学门户，读取本学期（或指定下学期）的课表，
生成可供日历 App 订阅的 iCalendar 文件，默认输出到本仓库的：

```
res/coursesAuto.ics
```

本仓库是独立项目，只负责「登录 → 读取课表 → 生成 .ics」。发布到
`sunbeamgus.github.io` 需要额外一步复制文件，见[发布到网站](#发布到网站)。

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

## 构建

要求：

- Windows 10/11 x64
- CMake ≥ 3.20
- MinGW-w64 g++（推荐，已在 GCC 16.2 上验证）或 MSVC

在仓库根目录执行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

产物：

```
build/thu-courses-ics.exe
```

也可以直接运行一键脚本（等价于上面两条命令）：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

先跑一遍内置测试（不联网、不需要账号）：

```powershell
.\build\thu-courses-ics.exe --self-test
```

测试覆盖 SM2 标准向量与加解密、SM3、JSON、日期换算、学期与课表解析、
ICS 生成与折行。

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
| `THU_USER_ID` | 是 | 学号 |
| `THU_PASSWORD` | 是 | 信息门户密码 |
| `THU_SEMESTER_INDEX` | 否 | 抓取下学期：`nextSemesterList` 下标，0 起；留空为当前学期 |
| `THU_OUTPUT` | 否 | 输出路径，默认 `res/coursesAuto.ics` |
| `THU_CALNAME` | 否 | 日历名称 |
| `THU_FINGERPRINT` | 否 | 设备指纹；默认按“学号 + 计算机名”生成并保持稳定 |
| `HTTPS_PROXY` | 否 | HTTP 代理，如 `http://127.0.0.1:15236`；默认直连 |

也可以使用同名命令行参数覆盖（见 `--help`）。**密码建议只写在 `.env`**，
避免出现在命令历史中。

> 如果账号启用了 TOTP 两步验证，登录时程序会暂停并提示：
> `请输入身份验证器 App 中的 6 位 TOTP 验证码`。输入当前 6 位数字即可，
> 不需要把密钥保存到任何文件；输入 `q` 可取消。

> 清华的登录入口只对国内网络开放；如果你使用代理，请让
> `*.tsinghua.edu.cn` 直连。GitHub Actions 等海外 runner 无法完成登录，
> 因此本工具设计为在本机运行。

## 生成

在仓库根目录执行（这样默认输出正好是 `res/coursesAuto.ics`）：

```powershell
.\build\thu-courses-ics.exe
```

或使用包装脚本（会自动切换工作目录、首次运行时构建）：

```powershell
powershell -ExecutionPolicy Bypass -File .\run.ps1
```

示例输出：

```
🔐 正在登录清华大学信息门户（2022xxxxxx）…
📚 正在读取学期信息…
📅 正在读取课表：2026-2027学年秋季学期…
✅ 已生成 C:\...\thu-ics-cpp\res\coursesAuto.ics
   学期：2026-2027学年秋季学期（第 1 周周一 = 2026-09-14，共 17 周）
   课程：23 门，事件：412 个
```

### 发布到网站

`res/` 被本仓库的 `.gitignore` 忽略（课表属于个人数据，不适合放在公开的代码仓库
历史里）。发布时把生成的文件交给网站仓库：

```powershell
# 一步完成：生成 + 复制到网站仓库的 res/coursesAuto.ics
powershell -ExecutionPolicy Bypass -File .\run.ps1 -Publish ..\sunbeamgus.github.io
```

然后在网站仓库里提交：

```powershell
cd ..\sunbeamgus.github.io
git add res/coursesAuto.ics
git commit -m "chore: update res/coursesAuto.ics"
git push
```

订阅地址为：

```
https://sunbeamgus.github.io/res/coursesAuto.ics
webcal://sunbeamgus.github.io/res/coursesAuto.ics
```

如果确认新文件正确，可以把 `index.html` / `index.md` 里的订阅链接从
`res/courses.ics` 切换到 `res/coursesAuto.ics`。

## 可选参数

```
--user-id <学号>         --password <密码>
--semester <N>           --output <路径>
--calname <名称>         --proxy <host:port>
--fingerprint <hex>      --dump-dir <目录>
--offline <目录>         --verbose
--self-test
```

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
schtasks /Create /SC DAILY /TN "THU Courses ICS" /ST 06:00 /TR "powershell -ExecutionPolicy Bypass -File C:\path\to\thu-ics-cpp\run.ps1 -Publish C:\path\to\sunbeamgus.github.io"
```

删除任务：

```powershell
schtasks /Delete /TN "THU Courses ICS" /F
```

任务只负责生成 `res\coursesAuto.ics` 并复制到网站仓库；如需自动发布，再在任务里
追加网站仓库的 `git add/commit/push`（或另写一个包装脚本）。

## 实现说明

### 登录

1. `GET https://webvpn.tsinghua.edu.cn/login?oauth_login=true`，跟随跳转；
2. 从 id.tsinghua.edu.cn 登录页读取 `#sm2publicKey`；
3. 用 SM2（C1C3C2，`04||C1||C3||C2`）加密密码，POST 到
   `/do/off/ui/auth/login/check`：
   `i_user` / `i_pass` / `fingerPrint` / `fingerGenPrint` / `i_captcha`；
4. 若返回“二次认证”，走 `b/doubleAuth/login` 的
   `FIND_APPROACHES` → `SEND_CODE(type=totp)`，然后提示你在终端**手动输入
   6 位 TOTP 验证码**，再调用 `VERITY_TOTP_CODE`；可同时信任本机指纹；
5. 从成功页取回调链接，建立 WebVPN 会话，再执行一次
   `form/10000ea...` 的 ID 登录和 `oauth.tsinghua.edu.cn/lb-auth/lbredirect`
   跳转。

验证码不会被保存，也不会出现在日志中；如果连续输错，会提示重新输入。

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
  无人值守的定时任务只有在“本设备已被信任”时才不会触发 2FA，否则会等待
  标准输入并报错。
- **二级课表**：页面格式没有公开接口，解析采用宽松匹配；失败时只警告并
  继续使用教学日历，不影响主课表。
- **自定义日程**：`getSchedule()` 不包含 App 本地添加的自定义日程，
  因此本工具也不会生成它们。
- **密码错误 / 需要验证**：错误信息会从登录页的 `msg_note` 提取；若显示
  “用户名或密码不正确”，请检查 `.env`。
- **数据隐私**：生成的 `.ics` 一旦发布就会公开托管，任何拿到 URL 的人都能看到
  你的课表；本仓库已在 `.gitignore` 中忽略 `res/` 与 `dump/`，如不接受公开，
  请不要发布到网站仓库。

## 目录

```
thu-ics-cpp/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── .env.example
├── build.ps1
├── run.ps1
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
