/* ------------------------------------------------------------------
   FATALITY injection animation — desktop overlay (D3D11 + D2D1 + DComp)

   Port of cs2_internal/src/menu/init/init_anim.cpp: same textures, same
   positions, same easing, same 75% black veil. The overlay is a click-
   through, always-on-top, per-pixel-alpha window (DirectComposition), so
   the letters and the veil composite straight onto whatever is beneath.

   Build (MinGW-w64, the one shipped with MSYS2). Run it from a shell where
   C:\msys64\mingw64\bin is on PATH, otherwise cc1plus can't find its DLLs:
     set PATH=C:\msys64\mingw64\bin;%PATH%
     g++ -O2 -std=c++17 fatality_overlay.cpp -o fatality_overlay.exe ^
         -mwindows -static -static-libgcc -static-libstdc++ ^
         -ld3d11 -ld2d1 -ldcomp -ldxgi -ldxguid -lwindowscodecs ^
         -lole32 -luuid -luser32 -lgdi32

   Behaviour: plays the intro once, then the process exits by itself.
   Pass --loop to keep it running (repeats every 1.2s).
   Keys: Ctrl+Alt+Q quits early, Ctrl+Alt+R replays.
   ------------------------------------------------------------------ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <wincodec.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "textures.h"

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

/* ---- accent colour, gui.h: MAKE_COLOR(accent, 235, 5, 90) ---------- */
static const float ACCENT_R = 235.f, ACCENT_G = 5.f, ACCENT_B = 90.f;

/* ---- timeline (identical numbers to the web version) --------------- */
static const float HOLD_END = 2.0f;   /* fly2 finishes here             */
static const float FADE_LEN = .2f;    /* whole thing fades out          */
static const float TOTAL    = HOLD_END + FADE_LEN;
static const float LOOP_GAP = 1.2f;

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float ease_out(float x) { x = clampf(x, 0.f, 1.f); float u = 1.f - x; return 1.f - u * u * u; }
static inline float prog(float t, float s, float d) { return clampf((t - s) / d, 0.f, 1.f); }

struct State { float toggle, letter, fly1, fly2; bool ending; };

static State timeline(float t)
{
	State s{};
	s.toggle = t < .25f ? t / .25f
	         : t < HOLD_END ? 1.f
	         : clampf(1.f - (t - HOLD_END) / FADE_LEN, 0.f, 1.f);
	s.letter = 2.f - ease_out(prog(t, .25f, .5f));    /* F: 2x -> 1x      */
	s.fly1   = 1.f - ease_out(prog(t, .75f, .5f));    /* F slides left    */
	s.fly2   = 1.f - ease_out(prog(t, 1.25f, .75f));  /* ATALITY rises    */
	s.ending = t >= .75f;
	return s;
}

/* ---- tiny logging ------------------------------------------------- */
static void logf_(const char *fmt, ...)
{
	char buf[1024];
	va_list ap; va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	char path[MAX_PATH]{};
	DWORD n = GetTempPathA(MAX_PATH, path);
	if (n && n < MAX_PATH - 32) {
		strncat(path, "fatality_overlay.log", MAX_PATH - strlen(path) - 1);
		if (FILE *f = fopen(path, "a")) { fputs(buf, f); fputc('\n', f); fclose(f); }
	}
}

/* ---- COM helpers -------------------------------------------------- */
template <class T> static inline void safe_release(T *&p) { if (p) { p->Release(); p = nullptr; } }

/* ---- globals ------------------------------------------------------ */
static HWND            g_hwnd;
static ID3D11Device           *g_dev;
static IDXGISwapChain1        *g_swap;
static IDCompositionDevice    *g_dcomp;
static IDCompositionTarget    *g_target;
static IDCompositionVisual    *g_visual;
static ID2D1Factory1          *g_d2d_factory;
static ID2D1Device            *g_d2d_dev;
static ID2D1DeviceContext     *g_dc;
static ID2D1SolidColorBrush   *g_veil;
static ID2D1Bitmap1           *g_tex_f200, *g_tex_f400, *g_tex_at;
static UINT g_w, g_h;
static bool g_quit;

/* ---- WIC -> premultiplied BGRA, tinted with the accent colour ------ */
static ID2D1Bitmap1 *load_texture(const unsigned char *png, UINT png_size,
                                  IWICImagingFactory *wic, UINT *out_w, UINT *out_h)
{
	IWICStream            *stream = nullptr;
	IWICBitmapDecoder     *dec    = nullptr;
	IWICBitmapFrameDecode *frame  = nullptr;
	IWICFormatConverter   *conv   = nullptr;
	ID2D1Bitmap1          *bmp    = nullptr;

	do {
		if (FAILED(wic->CreateStream(&stream))) break;
		if (FAILED(stream->InitializeFromMemory(const_cast<BYTE *>(png), png_size))) break;
		if (FAILED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &dec))) break;
		if (FAILED(dec->GetFrame(0, &frame))) break;
		if (FAILED(wic->CreateFormatConverter(&conv))) break;
		if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
		                            nullptr, 0.f, WICBitmapPaletteTypeCustom))) break;

		UINT w = 0, h = 0;
		if (FAILED(conv->GetSize(&w, &h))) break;

		std::vector<BYTE> px((size_t)w * h * 4);
		if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) break;

		/* premultiplied BGRA: scaling RGB by the accent keeps it valid */
		for (size_t i = 0; i < px.size(); i += 4) {
			px[i + 0] = (BYTE)(px[i + 0] * ACCENT_B / 255.f);   /* B */
			px[i + 1] = (BYTE)(px[i + 1] * ACCENT_G / 255.f);   /* G */
			px[i + 2] = (BYTE)(px[i + 2] * ACCENT_R / 255.f);   /* R */
		}

		D2D1_BITMAP_PROPERTIES1 props = {};
		props.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
		props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
		props.dpiX = props.dpiY = 96.f;
		props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;

		D2D1_SIZE_U sz = { w, h };
		if (FAILED(g_dc->CreateBitmap(sz, px.data(), w * 4, &props, &bmp))) break;

		*out_w = w; *out_h = h;
	} while (0);

	safe_release(stream); safe_release(dec); safe_release(frame); safe_release(conv);
	return bmp;
}

/* ---- window ------------------------------------------------------- */
static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
	case WM_DESTROY:
		g_quit = true;
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

static bool init_window()
{
	SetProcessDPIAware();

	WNDCLASSEXW wc{};
	wc.cbSize        = sizeof(wc);
	wc.lpfnWndProc   = wnd_proc;
	wc.hInstance     = GetModuleHandleW(nullptr);
	wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = L"fatality_overlay";
	if (!RegisterClassExW(&wc)) return false;

	/* primary monitor, in physical pixels */
	MONITORINFO mi{}; mi.cbSize = sizeof(mi);
	GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
	g_w = (UINT)(mi.rcMonitor.right - mi.rcMonitor.left);
	g_h = (UINT)(mi.rcMonitor.bottom - mi.rcMonitor.top);

	g_hwnd = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
		WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP,
		L"fatality_overlay", L"FATALITY", WS_POPUP,
		mi.rcMonitor.left, mi.rcMonitor.top, (int)g_w, (int)g_h,
		nullptr, nullptr, wc.hInstance, nullptr);
	if (!g_hwnd) return false;

	/* LAYERED without a colour key keeps the window out of hit-testing even
	   on compositors that ignore WS_EX_TRANSPARENT for popups */
	SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
	ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
	UpdateWindow(g_hwnd);
	return true;
}

/* ---- device / composition / d2d ----------------------------------- */
static bool init_d3d()
{
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
	D3D_FEATURE_LEVEL got{};

	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
	                               want, ARRAYSIZE(want), D3D11_SDK_VERSION,
	                               &g_dev, &got, nullptr);
	if (FAILED(hr))
		hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
		                       want, ARRAYSIZE(want), D3D11_SDK_VERSION,
		                       &g_dev, &got, nullptr);
	if (FAILED(hr)) { logf_("D3D11CreateDevice failed %08lx", (unsigned long)hr); return false; }

	IDXGIDevice *dxgi_dev = nullptr;
	if (FAILED(g_dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev))) return false;

	IDXGIAdapter *adapter = nullptr;
	dxgi_dev->GetAdapter(&adapter);

	IDXGIFactory2 *factory = nullptr;
	adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&factory);
	safe_release(adapter);

	DXGI_SWAP_CHAIN_DESC1 sd{};
	sd.Width       = g_w;
	sd.Height      = g_h;
	sd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.SampleDesc.Count = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.Scaling     = DXGI_SCALING_STRETCH;
	sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	sd.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;

	hr = factory->CreateSwapChainForComposition(g_dev, &sd, nullptr, &g_swap);
	safe_release(factory);
	if (FAILED(hr)) { logf_("CreateSwapChainForComposition failed %08lx", (unsigned long)hr); return false; }

	hr = DCompositionCreateDevice(dxgi_dev, __uuidof(IDCompositionDevice), (void **)&g_dcomp);
	if (FAILED(hr)) { logf_("DCompositionCreateDevice failed %08lx", (unsigned long)hr); return false; }

	g_dcomp->CreateTargetForHwnd(g_hwnd, TRUE, &g_target);
	g_dcomp->CreateVisual(&g_visual);
	g_visual->SetContent(g_swap);
	g_target->SetRoot(g_visual);
	g_dcomp->Commit();

	hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr, (void **)&g_d2d_factory);
	if (FAILED(hr)) { logf_("D2D1CreateFactory failed %08lx", (unsigned long)hr); return false; }

	if (FAILED(g_d2d_factory->CreateDevice(dxgi_dev, &g_d2d_dev))) return false;
	if (FAILED(g_d2d_dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_dc))) return false;
	g_dc->SetDpi(96.f, 96.f);
	g_dc->SetUnitMode(D2D1_UNIT_MODE_PIXELS);

	D2D1_COLOR_F black = { 0.f, 0.f, 0.f, 1.f };
	if (FAILED(g_dc->CreateSolidColorBrush(black, &g_veil))) return false;

	safe_release(dxgi_dev);
	return true;
}

static bool load_textures()
{
	IWICImagingFactory *wic = nullptr;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
	                            __uuidof(IWICImagingFactory), (void **)&wic))) {
		logf_("WIC factory failed");
		return false;
	}

	UINT w = 0, h = 0;
	g_tex_f200 = load_texture(tex_f200, tex_f200_size, wic, &w, &h);
	g_tex_f400 = load_texture(tex_f400, tex_f400_size, wic, &w, &h);
	g_tex_at   = load_texture(tex_atality, tex_atality_size, wic, &w, &h);
	safe_release(wic);

	logf_("textures: f200=%p f400=%p atality=%p", (void *)g_tex_f200, (void *)g_tex_f400, (void *)g_tex_at);
	return g_tex_f200 && g_tex_f400 && g_tex_at;
}

/* ---- one frame ---------------------------------------------------- */
static void render(float t)
{
	const State s = timeline(t);

	IDXGISurface *surf = nullptr;
	if (FAILED(g_swap->GetBuffer(0, __uuidof(IDXGISurface), (void **)&surf))) return;

	D2D1_BITMAP_PROPERTIES1 props = {};
	props.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
	props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
	props.dpiX = props.dpiY = 96.f;
	props.bitmapOptions = (D2D1_BITMAP_OPTIONS)(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW);

	ID2D1Bitmap1 *target = nullptr;
	if (FAILED(g_dc->CreateBitmapFromDxgiSurface(surf, &props, &target))) { surf->Release(); return; }
	surf->Release();

	g_dc->SetTarget(target);
	g_dc->BeginDraw();

	D2D1_COLOR_F clear = { 0.f, 0.f, 0.f, 0.f };
	g_dc->Clear(&clear);

	/* init_anim.cpp:125 — black veil over the whole display, alpha .75*toggle */
	if (s.toggle > .001f) {
		g_veil->SetOpacity(.75f * s.toggle);
		D2D1_RECT_F full = { 0.f, 0.f, (float)g_w, (float)g_h };
		g_dc->FillRectangle(&full, g_veil);
	}

	const float cx = g_w * .5f, cy = g_h * .5f;

	if (!s.ending) {
		/* f_400 drawn at size * letter * .5, alpha = 2 - letter */
		const float sw = 236.f * s.letter * .5f;
		const float sh = 400.f * s.letter * .5f;
		D2D1_RECT_F dst = { cx - sw * .5f, cy - sh * .5f, cx + sw * .5f, cy + sh * .5f };
		g_dc->DrawBitmap(g_tex_f400, &dst, clampf(2.f - s.letter, 0.f, 1.f),
		                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
	} else {
		const float fa_w = 118.f * .5f + 1073.f * .5f;              /* 595.5 */
		const float fx   = cx - 118.f * .5f - (fa_w - 118.f * .5f) * (1.f - s.fly1);
		D2D1_RECT_F fdst = { fx, cy - 100.f, fx + 118.f, cy + 100.f };
		g_dc->DrawBitmap(g_tex_f200, &fdst, clampf(s.toggle, 0.f, 1.f),
		                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);

		const float ax = cx - (fa_w - 118.f);
		const float ay = cy + 100.f + 200.f * s.fly2 - 200.f;
		D2D1_RECT_F adst = { ax, ay, ax + 1073.f, ay + 200.f };
		g_dc->DrawBitmap(g_tex_at, &adst,
		                 clampf(1.f - s.fly2, 0.f, 1.f) * clampf(s.toggle, 0.f, 1.f),
		                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
	}

	HRESULT hr = g_dc->EndDraw();
	if (hr == (HRESULT)D2DERR_RECREATE_TARGET) {
		logf_("D2D device lost");
	}
	g_dc->SetTarget(nullptr);
	target->Release();

	g_swap->Present(1, 0);      /* vsync */
}


void RunFatalityOverlay()
{
	bool loop = false;

	SetProcessDPIAware();
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hr)) return;

	logf_("--- start (mingw build, %s) ---", loop ? "loop" : "once");

	if (!init_window() || !init_d3d() || !load_textures()) return;

	LARGE_INTEGER freq, start, now;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);

	float t = 0.f, paused_at = -1.f;
	int frames = 0;

	for (;;) {
		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}


		QueryPerformanceCounter(&now);
		float elapsed = (float)((double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart);



		/* played to the end: leave the (now fully transparent) window and exit */
		if (elapsed > TOTAL) {
			if (!loop) break;
			if (paused_at < 0.f) paused_at = elapsed;
			else if (elapsed - paused_at > LOOP_GAP) { start = now; elapsed = 0.f; paused_at = -1.f; }
		}

		t = elapsed < TOTAL ? elapsed : TOTAL;
		render(t);
		if (++frames == 1) logf_("first frame drawn (%ux%u)", g_w, g_h);
	}

	logf_("--- exit after %d frames ---", frames);

	safe_release(g_tex_at); safe_release(g_tex_f400); safe_release(g_tex_f200);
	safe_release(g_veil); safe_release(g_dc); safe_release(g_d2d_dev); safe_release(g_d2d_factory);
	safe_release(g_visual); safe_release(g_target); safe_release(g_dcomp);
	safe_release(g_swap); safe_release(g_dev);
	if (g_hwnd) DestroyWindow(g_hwnd);
	CoUninitialize();
}
