<div align="center">

# BiliProxy

[![GitHub Release](https://img.shields.io/github/v/release/Qxyz17/BiliProxy?style=flat&color=%2300a1d6)](https://github.com/Qxyz17/BiliProxy/releases)
[![CI](https://img.shields.io/github/actions/workflow/status/Qxyz17/BiliProxy/build.yml?style=flat&label=build)](https://github.com/Qxyz17/BiliProxy/actions)
[![License](https://img.shields.io/badge/license-MIT-24292f?style=flat)](LICENSE)
[![Made by Qxyz17](https://img.shields.io/badge/made%20by-Qxyz17-00a1d6?style=flat)](https://github.com/Qxyz17)

</div>

---

## 它解决什么

| 痛点 | BiliProxy 的做法 |
| --- | --- |
| B 站默认下发 AV1 / HEVC 流，老显卡硬解不动 | 代理拦截 `playurl`，强制只留 H.264，清晰度不够就自动降档 |
| 未登录看视频 480P 流里烧了水印 | 网页一键下载，走无水印原始流 |
| 本地下载的视频同样是 HEVC / AV1 | 拖进窗口，内置 ffmpeg 转成兼容等级可调的 H.264 |
| 想知道视频讲了什么但懒得看完 | 边栏把字幕、弹幕、评论、画面帧交给 AI 总结，内容项与抽取参数全可调 |

## 功能

### 网页增强

- 本机 `127.0.0.1:18080` 起 HTTPS 代理，PAC 只接管 `api.bilibili.com` 与 `www.bilibili.com`，其余直连
- 拦截 `/x/player/playurl`：请求侧剥离 `fnval` AV1 位；响应侧只保留目标编码，缺失时按 1080P→720P→480P→360P 降档取源
- 屏蔽自动弹出的登录窗，手动登录不受影响，自己按的暂停不会被顶掉
- 每次打开 `www.bilibili.com` 注入一次 FATALITY 开场动画（约 3.2 秒，透明背景）
- **右侧边栏**：视频页贴右边缘，两个入口
  - **下载** — 无水印 / 有水印两种来源，实时进度，按 avid/cid 匹配当前视频
  - **对话** — 把标题、作者、简介与数据、字幕、弹幕、评论、画面帧交给 AI 总结或追问，Markdown 渲染回答

画面帧由代理用内置 ffmpeg 从当前视频流抽取，不打断播放器；帧数、帧宽、每分钟帧数、各项条数上限与提示词都可调。接口填 OpenAI 兼容地址或 Gemini 地址，Key 存在浏览器本地。

### 本地转码

- ffmpeg 经 zlib 压缩后直接编进 exe，首次运行解压到 `%LOCALAPPDATA%\BiliProxy\`，不携带独立 ffmpeg.exe
- 拖拽 / 选择文件 / 命令行批处理三种入口
- 目标编码可选 H.264 / H.265 / AV1，兼容等级 L4.0 / L4.1 / L5.0，可强制 Baseline
- 源为 H.264 但 profile/level 超标（如 High@5.1）时照样重编，不误跳过
- 输出 moov 前置、AAC 音频、超 1080p 自动缩放

### GUI

两个页签，GDI+ 自绘，无外部控件依赖：

- **网页增强** — 目标编码、屏蔽登录弹窗、开机自启
- **本地转码** — 编码、兼容等级、任务队列与进度

## 下载

从 [Releases](https://github.com/Qxyz17/BiliProxy/releases) 拿最新版：

- `BiliProxy.exe` — GUI 主程序（推荐）
- `BiliProxy-console.exe` — 命令行版

## 安装

1. 运行 `BiliProxy.exe`
2. 首次弹出根证书安装警告时点“是”——证书只装在当前用户 Root / TrustedPeople，只用于 B 站域名的本地 HTTPS 拦截
3. 程序自动：自拷到 `%LOCALAPPDATA%\BiliProxy\`、注册开机自启、写入 PAC、托盘盾牌图标
4. 重启一次 Edge 生效

卸载：程序内点“退出”或运行 `卸载.cmd`，证书、PAC、自启全部清除。

## 构建

依赖 MSYS2 MinGW-w64 工具链（g++ 17 + OpenSSL 3 + zlib + brotli）与 Python 3：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

页面侧边栏的前端代码在 `src/webui/`，改动后先重新生成内嵌头文件，再构建：

```powershell
python tools\pack_webui.py
```

CI 每次推送与打 tag 自动构建 Windows 产物，见 [Actions](https://github.com/Qxyz17/BiliProxy/actions)。

## 已知限制

- 只接管走系统代理的浏览器请求，B 站客户端与直播不经过代理
- 全部清晰度都无目标编码时原样放行
- AV1 编码用内置 libaom-av1，速度较慢

---

<div align="center">

**BiliProxy** · Made by [Qxyz17](https://github.com/Qxyz17)

</div>
