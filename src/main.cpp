#include "common.h"
#include "cert.h"
#include "config.h"
#include "gui.h"
#include "proxy.h"
#include "overlay.h"
#include "transcode.h"
#include "download.h"

int AppMain(HINSTANCE hInst, const std::wstring& cmd) {
    bool uninstall = cmd.find(L"--uninstall") != std::wstring::npos;
    bool debug = cmd.find(L"--debug") != std::wstring::npos;
    bool noInstall = cmd.find(L"--no-install") != std::wstring::npos;
    bool autostart = cmd.find(L"--autostart") != std::wstring::npos;
    if (cmd.find(L"--overlay") != std::wstring::npos) { RunFatalityOverlay(); return 0; }
    g_verbose = debug;

    // 不声明 DPI 感知时系统会把自绘窗口整体拉伸，圆角跟着走样
    SetProcessDPIAware();

    wchar_t appdata[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, MAX_PATH))
        SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    g_dir = std::wstring(appdata) + L"\\BiliProxy";

    if (cmd.find(L"--pac-watch") != std::wstring::npos) {
        size_t p = cmd.find(L"--pac-watch");
        DWORD pid = (DWORD)_wtoi(cmd.c_str() + p + 11);
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (h) {
            WaitForSingleObject(h, INFINITE);
            CloseHandle(h);
        }
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        setPac(false);
        WSACleanup();
        return 0;
    }

    CreateDirectoryW(g_dir.c_str(), nullptr);
    CreateDirectoryW((g_dir + L"\\certs").c_str(), nullptr);
    g_dirU8 = U8(g_dir);
    MigrateOldData();

    if (debug) {
        AllocConsole();
        freopen("CONOUT$", "w", stderr);
        freopen("CONOUT$", "w", stdout);
    }
    g_logFile = _wfopen((g_dir + L"\\proxy.log").c_str(), L"a");

    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    loadConfig();
    TranscodeInit();
    DlInit();
    loadLocalCfg();

    // 鍛戒护琛岄噷甯︾殑瑙嗛鏂囦欢锛堟嫋鍒?exe 涓?/ 鍙抽敭鈥滄墦寮€鏂瑰紡鈥濓級
    std::vector<std::wstring> files;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                std::wstring a = argv[i];
                if (a.rfind(L"--", 0) == 0) continue;
                DWORD attr = GetFileAttributesW(a.c_str());
                if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
                    files.push_back(a);
            }
            LocalFree(argv);
        }
    }

    // 绾懡浠よ杞爜锛欱iliProxy.exe --transcode a.mkv b.mp4
    if (cmd.find(L"--transcode") != std::wstring::npos) {
        if (files.empty()) { wprintf(L"no input file\n"); return 2; }
        TranscodeEnqueue(files);
        while (TranscodeActive()) Sleep(250);
        int n = TranscodeCount(), fail = 0;
        for (int i = 0; i < n; ++i) {
            TJob j;
            if (!TranscodeGet(i, &j)) continue;
            if (j.state == 5) { ++fail; }
            wprintf(L"[%d] %ls\n     %ls\n     %ls\n", j.state, j.path.c_str(),
                    j.out.c_str(), j.note.c_str());
        }
        return fail ? 1 : 0;
    }

    if (uninstall) {
        HWND w = FindWindowW(L"BiliProxyWnd", nullptr);
        if (w) { PostMessageW(w, WM_CLOSE, 0, 0); Sleep(400); }
        removeRootCa();
        setPac(false);
        RemoveAllAutostart();
        MessageBoxW(nullptr, L"确定要卸载 BiliProxy 吗？\n\n卸载会移除证书、PAC、自动启动等内容，\n但不会删除已下载的视频文件。",
                    L"BiliProxy", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    if (!genKeyCert() || !loadCerts()) {
        MessageBoxW(nullptr, L"无法生成或安装根证书，请以管理员身份运行并重试。", L"BiliProxy", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!noInstall) {
        if (!installRootCa()) {
            MessageBoxW(nullptr, L"无法安装根证书，HTTPS 流量可能无法拦截。", L"BiliProxy", MB_OK | MB_ICONWARNING);
            return 1;
        }
        RemoveAllAutostart();
        if (g_cfg.autostart) addAutostart(ensureInstalledCopy());
    }

    HANDLE hSingle = CreateMutexW(nullptr, TRUE, L"Local\\BiliProxySingleton");
    if (hSingle && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"BiliProxyWnd", nullptr);
        if (w) {
            if (!files.empty()) {
                std::wstring list;
                for (auto& f : files) { list += f; list += L'\n'; }
                COPYDATASTRUCT cds{};
                cds.dwData = 0x42504C46;   // 'BPLF'
                cds.cbData = (DWORD)((list.size() + 1) * sizeof(wchar_t));
                cds.lpData = (void*)list.c_str();
                DWORD_PTR res = 0;
                SendMessageTimeoutW(w, WM_COPYDATA, 0, (LPARAM)&cds,
                                    SMTO_ABORTIFHUNG, 3000, &res);
            }
            ShowWindow(w, SW_SHOW);
            SetForegroundWindow(w);
        }
        return 0;
    }

    if (!startProxy()) {
        MessageBoxW(nullptr, L"端口 18080 被占用，请关闭占用该端口的程序后重试。", L"BiliProxy", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 代理监听成功后才挂 PAC，否则浏览器会一直等一个不存在的 PAC 地址
    if (!noInstall) setPac(true);

    // 外层看门狗：本进程被强杀时子进程负责清 PAC，避免浏览器卡在死地址上
    if (!noInstall) {
        wchar_t self[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(self) + L"\" --pac-watch " +
                           std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    bool forceShow = !files.empty();
    if (!CreateMainWindow(hInst, !autostart || forceShow)) return 1;

    if (forceShow) {
        TranscodeEnqueue(files);
        ShowLocalPage();
    }

    if (!autostart && g_cfg.startupAnim) RunFatalityOverlay();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyGui();
    setPac(false);
    if (g_logFile) fclose(g_logFile);
    WSACleanup();
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmd, int) {
    return AppMain(hInst, lpCmd ? std::wstring(lpCmd) : std::wstring());
}

int wmain(int argc, wchar_t** argv) {
    std::wstring c;
    for (int i = 1; i < argc; ++i) { c += argv[i]; c += L" "; }
    return AppMain(GetModuleHandleW(nullptr), c);
}

