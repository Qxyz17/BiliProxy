#include "common.h"
#include "gui.h"
#include "config.h"
#include "transcode.h"

#include <gdiplus.h>
#include <shlobj.h>
#include <commdlg.h>
#include <cmath>

using namespace Gdiplus;

enum {
    IDC_CARD_AUTO = 1, IDC_CARD_AVC, IDC_CARD_HEVC, IDC_CARD_AV1,
    IDC_TOGGLE_NOLOGIN, IDC_TOGGLE_AUTOSTART,
    IDC_BTN_LOG, IDC_BTN_QUIT, IDC_BTN_CLOSE,
    IDC_TAB_WEB, IDC_TAB_LOCAL,
    IDC_DROP, IDC_BTN_CLEAR, IDC_BTN_OPENDIR,
    IDC_LC_AVC, IDC_LC_HEVC, IDC_LC_AV1,   // 本地转码目标编码
    IDC_LV_L40, IDC_LV_L41, IDC_LV_L50,    // 兼容等级
    IDC_LB_BASE,                           // 强制 baseline
    IDC_BTN_SETTINGS, IDC_SETTINGS_CLOSE, IDC_SETTINGS_MASK,
    IDC_TOGGLE_STARTUP_ANIM, IDC_TOGGLE_WEB_ANIM,
    IDC_TOGGLE_CHAT, IDC_TOGGLE_DOWNLOAD
};
static const int kW = 560;
static const int kH = 520;
static const int kCorner = 12;

// 画布按 DPI 放大，布局坐标仍按 96dpi 写，绘制时统一缩一次
static int g_scale = 100;    // 百分比
static int g_cw = kW, g_ch = kH;

static void InitScale() {
    HDC dc = GetDC(nullptr);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    if (dpi < 96) dpi = 96;
    g_scale = dpi * 100 / 96;
    g_cw = kW * g_scale / 100;
    g_ch = kH * g_scale / 100;
}

static const Color kWindowBg(255, 243, 243, 243);
static const Color kSurface(255, 255, 255, 255);
static const Color kControl(255, 251, 251, 251);
static const Color kControlHover(255, 240, 240, 240);
static const Color kBorder(255, 222, 222, 222);
static const Color kText(255, 26, 26, 26);
static const Color kSecondary(255, 93, 93, 93);
static const Color kAccent(255, 0, 95, 184);
static const Color kSelection(255, 227, 239, 250);
static const Color kSuccess(255, 15, 108, 62);
static const Color kDanger(255, 196, 43, 28);

static HWND g_hwnd = nullptr;
static NOTIFYICONDATAW g_nid{};
static ULONG_PTR g_gdiToken = 0;
static int g_hover = 0;
static int g_page = 0;   // 0=网页 1=本地

static RECT g_rcAuto{}, g_rcAvc{}, g_rcHevc{}, g_rcAv1{};
static RECT g_rcLog{}, g_rcQuit{}, g_rcClose{};
static RECT g_rcTabWeb{}, g_rcTabLocal{};
static RECT g_rcDrop{}, g_rcClear{}, g_rcOpenDir{};
static RECT g_rcLcAvc{}, g_rcLcHevc{}, g_rcLcAv1{};
static RECT g_rcLvL40{}, g_rcLvL41{}, g_rcLvL50{}, g_rcLbBase{};
static RECT g_rcSettings{}, g_rcSetPanel{}, g_rcSetClose{};
static RECT g_rcSetNoLogin{}, g_rcSetAutostart{};
static RECT g_rcSetStartup{}, g_rcSetWebAnim{}, g_rcSetChat{}, g_rcSetDownload{};
static bool g_settingsOpen = false;
static void InitLayout() {
    const int pad = 20, gap = 12;
    const int cw = (kW - pad * 2 - gap) / 2, ch = 72;
    int y = 120;
    SetRect(&g_rcAuto, pad, y, pad + cw, y + ch);
    SetRect(&g_rcAvc, pad + cw + gap, y, pad + cw + gap + cw, y + ch);
    y += ch + 12;
    SetRect(&g_rcHevc, pad, y, pad + cw, y + ch);
    SetRect(&g_rcAv1, pad + cw + gap, y, pad + cw + gap + cw, y + ch);
    SetRect(&g_rcLog, pad, 380, pad + 96, 380 + 34);
    SetRect(&g_rcQuit, kW - pad - 96, 380, kW - pad, 380 + 34);
    SetRect(&g_rcClose, kW - 40, 8, kW - 12, 36);
    SetRect(&g_rcSettings, kW - 76, 8, kW - 48, 36);

    // 设置浮层
    const int pw = 340, ph = 352;
    const int px = (kW - pw) / 2, py = (kH - ph) / 2;
    SetRect(&g_rcSetPanel, px, py, px + pw, py + ph);
    SetRect(&g_rcSetClose, px + pw - 32, py + 8, px + pw - 8, py + 32);
    SetRect(&g_rcSetNoLogin, px + 16, py + 56,  px + pw - 16, py + 92);
    SetRect(&g_rcSetAutostart, px + 16, py + 96, px + pw - 16, py + 132);
    SetRect(&g_rcSetStartup, px + 16, py + 136, px + pw - 16, py + 172);
    SetRect(&g_rcSetWebAnim, px + 16, py + 176, px + pw - 16, py + 212);
    SetRect(&g_rcSetChat,    px + 16, py + 216, px + pw - 16, py + 252);
    SetRect(&g_rcSetDownload, px + 16, py + 256, px + pw - 16, py + 292);

    SetRect(&g_rcTabWeb, pad, 52, pad + 96, 84);
    SetRect(&g_rcTabLocal, pad + 104, 52, pad + 200, 84);

    SetRect(&g_rcDrop, pad, 96, kW - pad, 172);

    // 本地转码：目标编码 3 个小卡片
    const int lcw = (kW - pad * 2 - 16) / 3;
    SetRect(&g_rcLcAvc,  pad,                       190, pad + lcw,             224);
    SetRect(&g_rcLcHevc, pad + lcw + 8,             190, pad + lcw * 2 + 8,     224);
    SetRect(&g_rcLcAv1,  pad + lcw * 2 + 16,        190, kW - pad,               224);

    // 兼容等级 3 个
    SetRect(&g_rcLvL40,  pad,                       244, pad + lcw,             272);
    SetRect(&g_rcLvL41,  pad + lcw + 8,             244, pad + lcw * 2 + 8,     272);
    SetRect(&g_rcLvL50,  pad + lcw * 2 + 16,        244, kW - pad,               272);
    SetRect(&g_rcLbBase, pad,                       280, kW - pad,               296);

    SetRect(&g_rcClear, pad, 462, pad + 116, 494);
    SetRect(&g_rcOpenDir, kW - pad - 116, 462, kW - pad, 494);
}

static void RoundPath(GraphicsPath& p, const RectF& r, float rad) {
    const float d = rad * 2;
    // 四个弧各自起笔，靠 CloseFigure 收口；最后一个弧的终点必须落回起点
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

static void FillRound(Graphics& g, const RECT& rc, float rad, const Color& fill,
                      const Color& border, float bw) {
    GraphicsPath p;
    RoundPath(p, RectF((REAL)rc.left + 0.5f, (REAL)rc.top + 0.5f,
                       (REAL)(rc.right - rc.left) - 1.0f, (REAL)(rc.bottom - rc.top) - 1.0f), rad);
    SolidBrush br(fill);
    g.FillPath(&br, &p);
    if (bw > 0) {
        Pen pen(border, bw);
        g.DrawPath(&pen, &p);
    }
}

static void DrawStr(Graphics& g, const wchar_t* s, const RECT& rc, float px,
                    const Color& c, bool bold = false, bool center = false, bool right = false) {
    FontFamily fam(L"Microsoft YaHei UI");
    Font f(&fam, px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    StringFormat sf;
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetAlignment(center ? StringAlignmentCenter : (right ? StringAlignmentFar : StringAlignmentNear));
    SolidBrush br(c);
    g.DrawString(s, -1, &f,
                 RectF((REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top)),
                 &sf, &br);
}

// 符号取自 Segoe MDL2 Assets，比手绘路径和文字符号都干净
static void DrawGlyph(Graphics& g, const wchar_t* ch, const RECT& rc, float px, const Color& c) {
    FontFamily fam(L"Segoe MDL2 Assets");
    if (fam.GetLastStatus() != Ok) return;
    Font f(&fam, px, FontStyleRegular, UnitPixel);
    StringFormat sf;
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetAlignment(StringAlignmentCenter);
    SolidBrush br(c);
    g.DrawString(ch, -1, &f,
                 RectF((REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top)),
                 &sf, &br);
}

static std::wstring Ellipsis(const std::wstring& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n > 1 ? n - 1 : n) + L"…";
}

// 小号选择条（本地转码页的编码/等级）
static void DrawPill(Graphics& g, const RECT& rc, const wchar_t* text, bool sel, bool hover) {
    Color bg = sel ? kSelection : (hover ? kControlHover : kSurface);
    FillRound(g, rc, 6.0f, bg, sel ? kAccent : kBorder, 1.0f);
    DrawStr(g, text, rc, 12.0f, sel ? kAccent : kText, sel, true);
}

static void DrawCard(Graphics& g, const RECT& rc, bool sel, bool hover, const wchar_t* title) {
    Color bg = sel ? kSelection : (hover ? kControlHover : kSurface);
    FillRound(g, rc, 8.0f, bg, sel ? kAccent : kBorder, 1.0f);
    RECT tr{ rc.left + 16, rc.top, rc.right - 44, rc.bottom };
    DrawStr(g, title, tr, 14.0f, sel ? kAccent : kText);

    REAL cx = (REAL)rc.right - 26.0f, cy = (REAL)(rc.top + rc.bottom) / 2.0f;
    Pen pen(sel ? kAccent : kSecondary, 1.4f);
    g.DrawEllipse(&pen, cx - 8.0f, cy - 8.0f, 16.0f, 16.0f);
    if (sel) {
        SolidBrush dot(kAccent);
        g.FillEllipse(&dot, cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    }
}

static void DrawSwitch(Graphics& g, const RECT& row, bool on, bool hover) {
    const float w = 40.0f, h = 20.0f;
    float x = (float)row.right - w;
    float y = (float)(row.top + row.bottom) / 2.0f - h / 2.0f;
    GraphicsPath p;
    RoundPath(p, RectF(x, y, w, h), h / 2.0f);
    Color track = on ? kAccent : (hover ? kControlHover : kControl);
    SolidBrush br(track);
    g.FillPath(&br, &p);
    Pen pen(on ? kAccent : kSecondary, 1.0f);
    g.DrawPath(&pen, &p);
    float kx = on ? x + w - 12.0f - 3.0f : x + 3.0f;
    SolidBrush knob(on ? Color(255, 255, 255, 255) : kSecondary);
    g.FillEllipse(&knob, kx, y + 4.0f, 12.0f, 12.0f);
}

static void DrawToggleRow(Graphics& g, const RECT& rc, const wchar_t* label, bool on, bool hover) {
    RECT lr{ rc.left, rc.top, rc.right - 56, rc.bottom };
    DrawStr(g, label, lr, 14.0f, kText);
    DrawSwitch(g, rc, on, hover);
}

static void DrawButton(Graphics& g, const RECT& rc, const wchar_t* text, bool hover) {
    FillRound(g, rc, 4.0f, hover ? kControlHover : kControl, kBorder, 1.0f);
    DrawStr(g, text, rc, 14.0f, kText, false, true);
}

static void DrawTabs(Graphics& g) {
    const wchar_t* names[2] = { L"网页增强", L"本地转码" };
    RECT* rcs[2] = { &g_rcTabWeb, &g_rcTabLocal };
    int ids[2] = { IDC_TAB_WEB, IDC_TAB_LOCAL };
    for (int i = 0; i < 2; ++i) {
        bool sel = (g_page == i);
        bool hov = (g_hover == ids[i]);
        FillRound(g, *rcs[i], 6.0f, sel ? kSelection : (hov ? kControlHover : kSurface),
                  sel ? kAccent : kBorder, 1.0f);
        DrawStr(g, names[i], *rcs[i], 13.0f, sel ? kAccent : kText, sel, true);
    }
}

static void DrawStatus(Graphics& g) {
    SolidBrush dot(kSuccess);
    g.FillEllipse(&dot, 20.0f, 342.0f, 8.0f, 8.0f);

    wchar_t buf[128];
    long rw = InterlockedCompareExchange(&g_rewrites, 0, 0);
    swprintf(buf, 128, L"127.0.0.1:%d · 已改写 %ld 次", kPort, rw);
    RECT sr{ 34, 336, kW - 20, 356 };
    DrawStr(g, buf, sr, 12.0f, kSecondary);
}

static void DrawWebPage(Graphics& g) {
    RECT lab{ 20, 96, 220, 116 };
    DrawStr(g, L"目标编码", lab, 12.0f, kSecondary);

    DrawCard(g, g_rcAuto, g_cfg.codec == 0, g_hover == IDC_CARD_AUTO, L"自动");
    DrawCard(g, g_rcAvc, g_cfg.codec == 7, g_hover == IDC_CARD_AVC, L"H.264");
    DrawCard(g, g_rcHevc, g_cfg.codec == 12, g_hover == IDC_CARD_HEVC, L"H.265");
    DrawCard(g, g_rcAv1, g_cfg.codec == 13, g_hover == IDC_CARD_AV1, L"AV1");

    DrawStatus(g);

    DrawButton(g, g_rcLog, L"打开日志", g_hover == IDC_BTN_LOG);
    DrawButton(g, g_rcQuit, L"退出", g_hover == IDC_BTN_QUIT);
}

static void DrawJobRow(Graphics& g, const RECT& rc, const TJob& j) {
    FillRound(g, rc, 6.0f, kSurface, kBorder, 1.0f);

    wchar_t name[MAX_PATH];
    const wchar_t* base = wcsrchr(j.path.c_str(), L'\\');
    base = base ? base + 1 : j.path.c_str();
    wcsncpy(name, base, MAX_PATH - 1);
    name[MAX_PATH - 1] = 0;

    RECT nr{ rc.left + 12, rc.top + 4, rc.right - 130, rc.top + 26 };
    DrawStr(g, Ellipsis(name, 30).c_str(), nr, 13.0f, kText);

    const wchar_t* st = L"";
    Color stc = kSecondary;
    wchar_t pct[32] = L"";
    switch (j.state) {
        case 0: st = L"排队"; break;
        case 1: st = L"分析中"; break;
        case 2: st = L"转码中"; stc = kAccent; swprintf(pct, 32, L" %d%%", j.percent); break;
        case 3: st = L"完成"; stc = kSuccess; break;
        case 4: st = L"跳过"; break;
        case 5: st = L"失败"; stc = kDanger; break;
    }
    std::wstring right = std::wstring(st) + pct;
    RECT sr{ rc.right - 124, rc.top + 4, rc.right - 12, rc.top + 26 };
    DrawStr(g, right.c_str(), sr, 12.0f, stc, false, false, true);

    if (j.state == 2) {
        RECT tr{ rc.left + 12, rc.top + 30, rc.right - 12, rc.top + 34 };
        FillRound(g, tr, 2.0f, Color(255, 234, 234, 234), Color(0, 0, 0, 0), 0);
        RECT fr = tr;
        fr.right = tr.left + (LONG)((tr.right - tr.left) * (j.percent / 100.0f));
        if (fr.right > fr.left) FillRound(g, fr, 2.0f, kAccent, Color(0, 0, 0, 0), 0);
    } else if (!j.note.empty()) {
        RECT nr2{ rc.left + 12, rc.top + 26, rc.right - 12, rc.top + 42 };
        DrawStr(g, Ellipsis(j.note, 46).c_str(), nr2, 11.0f, stc);
    }
}

static void DrawLocalPage(Graphics& g) {
    bool tools = TranscodeToolsPresent();
    FillRound(g, g_rcDrop, 10.0f, g_hover == IDC_DROP ? kControlHover : kSurface,
              kBorder, 1.0f);
    RECT t1{ g_rcDrop.left, g_rcDrop.top, g_rcDrop.right, g_rcDrop.bottom };
    DrawStr(g, tools ? L"把视频拖到这里，或点击选择文件" : L"缺少 ffmpeg",
            t1, 14.0f, tools ? kText : kDanger, false, true);

    // ---- 目标编码 ----
    int lcodec = 7, llevel = 40; bool lbase = false;
    TranscodeGetCfg(&lcodec, &llevel, &lbase);

    RECT l1{ 20, 174, 200, 190 };
    DrawStr(g, L"目标编码", l1, 11.0f, kSecondary);
    DrawPill(g, g_rcLcAvc,  L"H.264", lcodec == 7,  g_hover == IDC_LC_AVC);
    DrawPill(g, g_rcLcHevc, L"H.265", lcodec == 12, g_hover == IDC_LC_HEVC);
    DrawPill(g, g_rcLcAv1,  L"AV1",   lcodec == 13, g_hover == IDC_LC_AV1);

    // ---- 兼容等级 ----
    RECT l2{ 20, 230, 400, 246 };
    DrawStr(g, L"兼容等级", l2, 11.0f, kSecondary);
    DrawPill(g, g_rcLvL40, L"L4.0", llevel == 40, g_hover == IDC_LV_L40);
    DrawPill(g, g_rcLvL41, L"L4.1", llevel == 41, g_hover == IDC_LV_L41);
    DrawPill(g, g_rcLvL50, L"L5.0", llevel == 50, g_hover == IDC_LV_L50);

    DrawToggleRow(g, g_rcLbBase, L"极限兼容",
                  lbase, g_hover == IDC_LB_BASE);

    int n = TranscodeCount();
    int shown = 0;
    for (int i = 0; i < n && shown < 4; ++i, ++shown) {
        TJob j;
        if (!TranscodeGet(i, &j)) continue;
        RECT rc{ 20, 306 + shown * 40, kW - 20, 306 + shown * 40 + 36 };
        DrawJobRow(g, rc, j);
    }
    if (n > 4) {
        wchar_t buf[64];
        swprintf(buf, 64, L"另有 %d 个任务未显示", n - 4);
        RECT mr{ 20, 306 + 4 * 40 - 2, kW - 20, 306 + 4 * 40 + 12 };
        DrawStr(g, buf, mr, 11.0f, kSecondary, false, true);
    }

    DrawButton(g, g_rcClear, L"清除已完成", g_hover == IDC_BTN_CLEAR);
    DrawButton(g, g_rcOpenDir, L"打开输出目录", g_hover == IDC_BTN_OPENDIR);
}

static void DrawGear(Graphics& g, const RECT& rc, bool hover) {
    if (hover) FillRound(g, rc, 6.0f, kControlHover, kBorder, 1.0f);
    DrawGlyph(g, L"\uE713", rc, 17.0f, kSecondary);
}

static void DrawClose(Graphics& g, const RECT& rc, bool hover) {
    if (hover) FillRound(g, rc, 6.0f, kControlHover, kBorder, 1.0f);
    DrawGlyph(g, L"\uE8BB", rc, 11.0f, kSecondary);
}

static void DrawSettingsOverlay(Graphics& g) {
    SolidBrush mask(Color(120, 0, 0, 0));
    g.FillRectangle(&mask, 0, 0, kW, kH);

    FillRound(g, g_rcSetPanel, 10.0f, kSurface, kBorder, 1.0f);
    RECT tr{ g_rcSetPanel.left + 16, g_rcSetPanel.top + 10, g_rcSetPanel.right - 40, g_rcSetPanel.top + 38 };
    DrawStr(g, L"设置", tr, 15.0f, kText, true);

    if (g_hover == IDC_SETTINGS_CLOSE) FillRound(g, g_rcSetClose, 4.0f, kControlHover, kBorder, 1.0f);
    DrawGlyph(g, L"\uE8BB", g_rcSetClose, 10.0f, kSecondary);

    DrawToggleRow(g, g_rcSetNoLogin, L"屏蔽未登录弹窗", g_cfg.blockLogin, g_hover == IDC_TOGGLE_NOLOGIN);
    DrawToggleRow(g, g_rcSetAutostart, L"开机自启", g_cfg.autostart, g_hover == IDC_TOGGLE_AUTOSTART);
    DrawToggleRow(g, g_rcSetStartup, L"启动动画", g_cfg.startupAnim, g_hover == IDC_TOGGLE_STARTUP_ANIM);
    DrawToggleRow(g, g_rcSetWebAnim, L"网页动画", g_cfg.webAnim, g_hover == IDC_TOGGLE_WEB_ANIM);
    DrawToggleRow(g, g_rcSetChat, L"对话", g_cfg.chatPanel, g_hover == IDC_TOGGLE_CHAT);
    DrawToggleRow(g, g_rcSetDownload, L"下载", g_cfg.downloadBtn, g_hover == IDC_TOGGLE_DOWNLOAD);
}

static void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    // 分层窗口要逐像素 alpha，圆角边缘才能抗锯齿；GDI 的 BitBlt 只有 1 位透明
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = g_cw;
    bi.bmiHeader.biHeight = -g_ch;               // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return;
    }
    HGDIOBJ old = SelectObject(mem, bmp);
    memset(bits, 0, (size_t)g_cw * g_ch * 4);

    Graphics g(mem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.ScaleTransform((REAL)g_scale / 100.0f, (REAL)g_scale / 100.0f);

    // 圆角外的像素保持全透明，交给 DWM 合成
    SolidBrush bg(kWindowBg);
    GraphicsPath bgp;
    RoundPath(bgp, RectF(0, 0, (REAL)kW, (REAL)kH), (REAL)kCorner);
    g.FillPath(&bg, &bgp);

    RECT tr{ 20, 6, kW - 56, 42 };
    DrawStr(g, L"BiliProxy", tr, 24.0f, kText, true);
    DrawGear(g, g_rcSettings, g_hover == IDC_BTN_SETTINGS);
    DrawClose(g, g_rcClose, g_hover == IDC_BTN_CLOSE);

    DrawTabs(g);

    if (g_page == 0) DrawWebPage(g);
    else DrawLocalPage(g);

    RECT ft{ 20, 494, kW - 20, 512 };
    DrawStr(g, L"Made by Qxyz17", ft, 11.0f, kSecondary, false, false, true);

    if (g_settingsOpen) DrawSettingsOverlay(g);
    g.Flush(FlushIntentionSync);

    // 不传 pptDst 时窗口位置会跟着每次重绘漂，必须把窗口左上角一起给出
    RECT wr{};
    GetWindowRect(hwnd, &wr);
    POINT dst{ wr.left, wr.top };
    POINT src{ 0, 0 };
    SIZE sz{ g_cw, g_ch };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, hdc, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static int HitTest(POINT pt) {
    // 布局按 96dpi 写，鼠标坐标要换回同一空间
    if (g_scale != 100) {
        pt.x = pt.x * 100 / g_scale;
        pt.y = pt.y * 100 / g_scale;
    }
    if (g_settingsOpen) {
        if (PtInRect(&g_rcSetClose, pt)) return IDC_SETTINGS_CLOSE;
        if (PtInRect(&g_rcSetNoLogin, pt)) return IDC_TOGGLE_NOLOGIN;
        if (PtInRect(&g_rcSetAutostart, pt)) return IDC_TOGGLE_AUTOSTART;
        if (PtInRect(&g_rcSetStartup, pt)) return IDC_TOGGLE_STARTUP_ANIM;
        if (PtInRect(&g_rcSetWebAnim, pt)) return IDC_TOGGLE_WEB_ANIM;
        if (PtInRect(&g_rcSetChat, pt)) return IDC_TOGGLE_CHAT;
        if (PtInRect(&g_rcSetDownload, pt)) return IDC_TOGGLE_DOWNLOAD;
        if (PtInRect(&g_rcSetPanel, pt)) return 0;
        return IDC_SETTINGS_MASK;
    }
    if (PtInRect(&g_rcSettings, pt)) return IDC_BTN_SETTINGS;
    if (PtInRect(&g_rcClose, pt)) return IDC_BTN_CLOSE;
    if (PtInRect(&g_rcTabWeb, pt)) return IDC_TAB_WEB;
    if (PtInRect(&g_rcTabLocal, pt)) return IDC_TAB_LOCAL;
    if (g_page == 0) {
        if (PtInRect(&g_rcAuto, pt)) return IDC_CARD_AUTO;
        if (PtInRect(&g_rcAvc, pt)) return IDC_CARD_AVC;
        if (PtInRect(&g_rcHevc, pt)) return IDC_CARD_HEVC;
        if (PtInRect(&g_rcAv1, pt)) return IDC_CARD_AV1;
        if (PtInRect(&g_rcLog, pt)) return IDC_BTN_LOG;
        if (PtInRect(&g_rcQuit, pt)) return IDC_BTN_QUIT;
    } else {
        if (PtInRect(&g_rcDrop, pt)) return IDC_DROP;
        if (PtInRect(&g_rcClear, pt)) return IDC_BTN_CLEAR;
        if (PtInRect(&g_rcOpenDir, pt)) return IDC_BTN_OPENDIR;
        if (PtInRect(&g_rcLcAvc, pt)) return IDC_LC_AVC;
        if (PtInRect(&g_rcLcHevc, pt)) return IDC_LC_HEVC;
        if (PtInRect(&g_rcLcAv1, pt)) return IDC_LC_AV1;
        if (PtInRect(&g_rcLvL40, pt)) return IDC_LV_L40;
        if (PtInRect(&g_rcLvL41, pt)) return IDC_LV_L41;
        if (PtInRect(&g_rcLvL50, pt)) return IDC_LV_L50;
        if (PtInRect(&g_rcLbBase, pt)) return IDC_LB_BASE;
    }
    return 0;
}

// 本地转码参数（不是网页改写参数）
static void ApplyLocalSetting(int id) {
    int codec = 7, level = 40; bool base = false;
    TranscodeGetCfg(&codec, &level, &base);
    switch (id) {
        case IDC_LC_AVC:  codec = 7;  break;
        case IDC_LC_HEVC: codec = 12; break;
        case IDC_LC_AV1:  codec = 13; break;
        case IDC_LV_L40:  level = 40; break;
        case IDC_LV_L41:  level = 41; break;
        case IDC_LV_L50:  level = 50; break;
        case IDC_LB_BASE: base = !base; break;
        default: return;
    }
    TranscodeSetCfg(codec, level, base);
    saveLocalCfg(codec, level, base);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void ApplySetting(int id) {
    switch (id) {
        case IDC_CARD_AUTO: g_cfg.codec = 0; break;
        case IDC_CARD_AVC:  g_cfg.codec = 7; break;
        case IDC_CARD_HEVC: g_cfg.codec = 12; break;
        case IDC_CARD_AV1:  g_cfg.codec = 13; break;
        case IDC_TOGGLE_NOLOGIN: g_cfg.blockLogin = !g_cfg.blockLogin; break;
        case IDC_TOGGLE_AUTOSTART: g_cfg.autostart = !g_cfg.autostart; break;
        case IDC_TOGGLE_STARTUP_ANIM: g_cfg.startupAnim = !g_cfg.startupAnim; break;
        case IDC_TOGGLE_WEB_ANIM: g_cfg.webAnim = !g_cfg.webAnim; break;
        case IDC_TOGGLE_CHAT: g_cfg.chatPanel = !g_cfg.chatPanel; break;
        case IDC_TOGGLE_DOWNLOAD: g_cfg.downloadBtn = !g_cfg.downloadBtn; break;
        default: return;
    }
    saveConfig();
    if (id == IDC_TOGGLE_AUTOSTART) {
        if (g_cfg.autostart) addAutostart(ensureInstalledCopy());
        else removeAutostart();
    }
    if (id == IDC_TOGGLE_NOLOGIN) setPac(true);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void PickFiles() {
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"视频文件\0*.mp4;*.mkv;*.webm;*.mov;*.flv;*.avi;*.ts;*.m4v;*.wmv;*.mpg;*.mpeg\0所有文件\0*.*\0";
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = (DWORD)buf.size();
    ofn.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    std::vector<std::wstring> files;
    const wchar_t* p = buf.data();
    std::wstring first(p);
    p += first.size() + 1;
    if (*p == 0) {
        files.push_back(first);
    } else {
        while (*p) {
            std::wstring name(p);
            p += name.size() + 1;
            files.push_back(first + L"\\" + name);
        }
    }
    TranscodeEnqueue(files);
    g_page = 1;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void HandleDrop(HDROP h) {
    UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> files;
    for (UINT i = 0; i < n; ++i) {
        UINT len = DragQueryFileW(h, i, nullptr, 0);
        std::wstring s(len + 1, 0);
        DragQueryFileW(h, i, &s[0], len + 1);
        s.resize(len);
        DWORD attr = GetFileAttributesW(s.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        files.push_back(s);
    }
    DragFinish(h);
    if (files.empty()) return;
    TranscodeEnqueue(files);
    g_page = 1;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void TrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"显示主界面");
    AppendMenuW(m, MF_STRING, 2, L"打开日志");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 3, L"退出");
    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    if (cmd == 1) { ShowWindow(g_hwnd, SW_SHOW); SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); SetForegroundWindow(g_hwnd); }
    else if (cmd == 2) ShellExecuteW(nullptr, L"open", (g_dir + L"\\proxy.log").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == 3) PostQuitMessage(0);
}

static void TrayAdd(HWND h) {
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = h;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP + 1;
    g_nid.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_SHIELD);
    wcsncpy(g_nid.szTip, L"BiliProxy", 127);
    g_nid.szTip[127] = 0;
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_APP + 1:
            if (l == WM_RBUTTONUP) TrayMenu();
            else if (l == WM_LBUTTONDBLCLK) { ShowWindow(h, SW_SHOW); SetForegroundWindow(h); }
            return 0;

        case WM_DROPFILES:
            HandleDrop((HDROP)w);
            return 0;

        case WM_COPYDATA: {
            const COPYDATASTRUCT* cds = (const COPYDATASTRUCT*)l;
            if (cds && cds->dwData == 0x42504C46 && cds->lpData) {
                std::wstring list((const wchar_t*)cds->lpData);
                std::vector<std::wstring> files;
                size_t s = 0;
                while (s < list.size()) {
                    size_t e = list.find(L'\n', s);
                    if (e == std::wstring::npos) e = list.size();
                    if (e > s) files.push_back(list.substr(s, e - s));
                    s = e + 1;
                }
                if (!files.empty()) {
                    TranscodeEnqueue(files);
                    g_page = 1;
                    ShowWindow(h, SW_SHOW);
                    SetForegroundWindow(h);
                    InvalidateRect(h, nullptr, FALSE);
                }
            }
            return TRUE;
        }

        case WM_TIMER:
            if (TranscodeActive()) InvalidateRect(h, nullptr, FALSE);
            return 0;

        case WM_PAINT:
            OnPaint(h);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            int id = HitTest(pt);
            if (id != g_hover) {
                g_hover = id;
                InvalidateRect(h, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{ sizeof tme, TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
            if (g_hover) { g_hover = 0; InvalidateRect(h, nullptr, FALSE); }
            return 0;

        case WM_LBUTTONUP: {
            POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            int id = HitTest(pt);
            if (id == IDC_SETTINGS_MASK) {
                g_settingsOpen = false;
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            if (id == IDC_SETTINGS_CLOSE) {
                g_settingsOpen = false;
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            if (id == IDC_BTN_SETTINGS) {
                g_settingsOpen = !g_settingsOpen;
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            if (id == IDC_BTN_CLOSE) { ShowWindow(h, SW_HIDE); return 0; }
            if (id == IDC_BTN_QUIT) { PostQuitMessage(0); return 0; }
            if (id == IDC_BTN_LOG) {
                ShellExecuteW(nullptr, L"open", (g_dir + L"\\proxy.log").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            if (id == IDC_TAB_WEB || id == IDC_TAB_LOCAL) {
                g_page = (id == IDC_TAB_WEB) ? 0 : 1;
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            if (id == IDC_DROP) { PickFiles(); return 0; }
            if (id == IDC_BTN_CLEAR) {
                TranscodeClearDone();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            if (id == IDC_BTN_OPENDIR) {
                std::wstring d = TranscodeLastOutDir();
                if (d.empty()) {
                    wchar_t vids[MAX_PATH];
                    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYVIDEO, nullptr, 0, vids))) d = vids;
                }
                if (!d.empty()) ShellExecuteW(nullptr, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            if (id == IDC_LC_AVC || id == IDC_LC_HEVC || id == IDC_LC_AV1 ||
                id == IDC_LV_L40 || id == IDC_LV_L41 || id == IDC_LV_L50 ||
                id == IDC_LB_BASE) {
                ApplyLocalSetting(id);
                return 0;
            }
            ApplySetting(id);
            return 0;
        }

        case WM_NCHITTEST: {
            POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            ScreenToClient(h, &pt);
            if (g_scale != 100) {
                pt.x = pt.x * 100 / g_scale;
                pt.y = pt.y * 100 / g_scale;
            }
            if (g_settingsOpen) return HTCLIENT;
            if (pt.y < 48 && !PtInRect(&g_rcClose, pt) && !PtInRect(&g_rcSettings, pt)) return HTCAPTION;
            return HTCLIENT;
        }

        case WM_MOVE:
        case WM_SIZE:
            // 分层窗口移动后位图留在旧坐标，得重新提交一次
            InvalidateRect(h, nullptr, FALSE);
            return 0;

        case WM_CLOSE:
            ShowWindow(h, SW_HIDE);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool CreateMainWindow(HINSTANCE hInst, bool show) {
    InitScale();
    InitLayout();
    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdiToken, &gsi, nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"BiliProxyWnd";
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_SHIELD);
    wc.style = 0;
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED, L"BiliProxyWnd", L"BiliProxy", WS_POPUP,
                             (sw - g_cw) / 2, (sh - g_ch) / 2, g_cw, g_ch,
                             nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return false;

    DragAcceptFiles(g_hwnd, TRUE);
    SetTimer(g_hwnd, 1, 250, nullptr);
    TrayAdd(g_hwnd);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ShowWindow(g_hwnd, show ? SW_SHOW : SW_HIDE);
    if (show) SetForegroundWindow(g_hwnd);
    return true;
}

void DestroyGui() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    GdiplusShutdown(g_gdiToken);
}

void ShowLocalPage() {
    g_page = 1;
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}
