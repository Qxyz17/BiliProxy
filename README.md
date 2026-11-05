# BiliProxy

本地 HTTPS 代理 + 本地视频转码。

## 网页增强

- 本机`127.0.0.1:18080`起 HTTPS 代理，系统 PAC 只把`api.bilibili.com`和`www.bilibili.com`（可选）指过去，其余全部 DIRECT。
- 拦截`/x/player/playurl`：
  - 请求侧把`fnval`的 AV1 位（2048）去掉，服务端不下发 AV1-only 的流；
  - 响应侧只保留目标编码的`dash.video`条目；该清晰度没有就按 1080P→720P→480P→360P 重新取源，全都没有才原样放行。
- 屏蔽未登录弹窗：只删自动弹出的登录窗，手动点登录不受影响，视频照常续播。
- 打开`www.bilibili.com`时注入一次 FATALITY 动画（透明背景，约 3.2 秒后自动移除）。
- 视频页注入下载按钮（右侧悬浮），按 avid/cid 匹配当前视频，支持无水印/有水印两种来源，附进度条。
- 附加`/__biliproxy/fatality.html`、`/__biliproxy/dl`、`/__biliproxy/dlstatus`供注入的页面使用。

GUI 两个页签：

- **网页增强**：目标编码（自动 / H.264 / H.265 / AV1，单选）、屏蔽未登录弹窗、开机自启。
- **本地转码**：目标编码（H.264 / H.265 / AV1）、兼容等级（L4.0 / L4.1 / L5.0）、极限兼容（Baseline）。

## 本地转码

- ffmpeg 压缩后直接编进 exe（objcopy 嵌段 + zlib 解压），首次使用时解到`%LOCALAPPDATA%\BiliProxy\ffmpeg.exe`，不再带独立的 ffmpeg.exe。
- 拖文件到窗口、点“选择文件”、或把视频拖到 exe 图标上（多开时经 WM_COPYDATA 转给已运行实例）。
- 输出`<原文件名>.h264.mp4`（按目标编码对应.h265.mp4 / .av1.mp4），moov 前置，音频转 AAC。
- 超过 1080p 自动缩到 1080p。
- 源文件编码名对但 profile/level 超出所选等级（比如 High@5.1）时不会跳过，照样重编；只有低于等级上限、yuv420p、AAC 时才标记“无需转换”。
- 命令行批处理：`BiliProxy-console.exe --transcode a.mkv b.mp4`。

## 安装

1. 双击`安装.cmd`（或直接运行`bin\BiliProxy.exe`）。
2. 首次运行弹“安全警告”（安装本地根证书）时点“是”。证书只装在当前用户的 Root / TrustedPeople，只用于给 B 站域名签本地证书。
3. 安装后：自拷到`%LOCALAPPDATA%\BiliProxy\BiliProxy.exe`、写开机自启（`HKCU\...\Run`）、写 PAC（WinINET`AutoConfigURL`）、托盘出现盾牌图标。
4. 重启一次 Edge 让代理设置生效。

## 卸载

双击`卸载.cmd`（即`--uninstall`）：删根证书、关 PAC、删自启。`%LOCALAPPDATA%\BiliProxy\`下的证书和日志可手动删。

## 重新编译

依赖：`C:\msys64\mingw64`（g++ 17 + OpenSSL 3 静态库 + zlib + brotli），`third_party\ffmpeg.exe`（gyan essentials，打进 exe）。

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

产物：`bin\BiliProxy.exe`（GUI，单文件，ffmpeg 内嵌）、`bin\BiliProxy-console.exe`（调试/命令行转码）。

## 验证

```powershell
Get-Content "$env:LOCALAPPDATA\BiliProxy\proxy.log" -Tail 20
node tools\probe-codec.js    # headless Edge 走代理打开视频页，检查 dash.video 只剩 codecid=7 且在播
node tools\probe-dl.js       # headless Edge 走代理点下载按钮，检查 avid/cid 匹配与产物
```

## 限制

- 只处理走系统代理的浏览器请求；B 站客户端、直播不经过它。
- 网页侧若所有清晰度都没有目标编码，原样放行。
- AV1 转码用内置的 libaom-av1，速度慢。

---

Made by [Qxyz17](https://github.com/Qxyz17)
