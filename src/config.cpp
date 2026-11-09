#include "common.h"
#include "config.h"
#include "transcode.h"

static bool runHidden(const std::wstring& cmdline, DWORD* exitCode = nullptr) {
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::wstring cl = cmdline;
    BOOL ok = CreateProcessW(nullptr, &cl[0], nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exitCode) *exitCode = code;
    return true;
}


std::string pacScript() {
    char buf[1600];
    snprintf(buf, sizeof buf,
        "function FindProxyForURL(url, host) {\n"
        "  if (host == \"api.bilibili.com\" || host == \"api.bilibili.tv\")\n"
        "    return \"PROXY 127.0.0.1:%d; DIRECT\";\n"
        "  if (%d && host == \"www.bilibili.com\")\n"
        "    return \"PROXY 127.0.0.1:%d; DIRECT\";\n"
        "  return \"DIRECT\";\n"
        "}\n", kPort, g_cfg.blockLogin ? 1 : 0, kPort);
    return buf;
}

void setPac(bool on) {
    INTERNET_PER_CONN_OPTION_LIST list{};
    INTERNET_PER_CONN_OPTION opt[2]{};
    list.dwSize = sizeof list;
    list.pszConnection = nullptr;
    list.dwOptionCount = on ? 2 : 1;
    list.pOptions = opt;
    opt[0].dwOption = INTERNET_PER_CONN_FLAGS;
    opt[0].Value.dwValue = on ? (PROXY_TYPE_AUTO_PROXY_URL | PROXY_TYPE_DIRECT) : PROXY_TYPE_DIRECT;
    static wchar_t url[160];
    if (on) {
        swprintf(url, 160, L"http://127.0.0.1:%d/proxy.pac?v=%lu", kPort, ++g_pacVersion);
        opt[1].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
        opt[1].Value.pszValue = url;
    }
    BOOL ok = InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, list.dwSize);
    Log("setPac(%d) per-connection=%d", on ? 1 : 0, ok ? 1 : 0);
    if (ok) InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    Log("PAC %s", on ? "enabled" : "disabled");
}

void addAutostart(const std::wstring& exePath) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    std::wstring v = L"\"" + exePath + L"\" --autostart";
    RegSetKeyValueW(k, nullptr, kRunValue, REG_SZ, v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    Log("autostart registered: %ls", exePath.c_str());
}

// 把自己复制到 %LOCALAPPDATA%\BiliProxy，避免用户移动/删除源码目录后自启失效
std::wstring ensureInstalledCopy() {
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring exeDir(self);
    size_t sl = exeDir.find_last_of(L"\\/");
    if (sl != std::wstring::npos) exeDir = exeDir.substr(0, sl);

    std::wstring target = g_dir + L"\\BiliProxy.exe";
    if (_wcsicmp(self, target.c_str()) == 0) return target;

    // 顺带把 ffmpeg.exe 一起放到安装目录（本地转码要用）
    std::wstring ffSrc = exeDir + L"\\ffmpeg.exe";
    std::wstring ffDst = g_dir + L"\\ffmpeg.exe";
    if (fileExists(ffSrc) && !fileExists(ffDst)) {
        if (CopyFileW(ffSrc.c_str(), ffDst.c_str(), FALSE)) Log("copied ffmpeg.exe to %ls", ffDst.c_str());
        else Log("copy ffmpeg failed %lu", GetLastError());
    }

    if (CopyFileW(self, target.c_str(), FALSE)) {
        Log("copied self to %ls", target.c_str());
        return target;
    }
    DWORD err = GetLastError();
    if (fileExists(target)) {   // 旧副本可用（通常是正在运行占用文件）
        Log("copy self failed %lu, keep existing %ls", err, target.c_str());
        return target;
    }
    Log("copy self failed %lu", err);
    return self;
}

void removeAutostart() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    RegDeleteKeyValueW(k, nullptr, kRunValue);
    RegCloseKey(k);
}

// ------------------------------------------------------------------ HTTP 解析

struct Header { std::string name, value; };


void loadConfig() {
    std::ifstream f((g_dir + L"\\config.ini").c_str());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq + 1));
        if (k == "codec") g_cfg.codec = atoi(v.c_str());
        else if (k == "blockLogin") g_cfg.blockLogin = atoi(v.c_str()) != 0;
        else if (k == "autostart") g_cfg.autostart = atoi(v.c_str()) != 0;
        else if (k == "startupAnim") g_cfg.startupAnim = atoi(v.c_str()) != 0;
        else if (k == "webAnim") g_cfg.webAnim = atoi(v.c_str()) != 0;
        else if (k == "chatPanel") g_cfg.chatPanel = atoi(v.c_str()) != 0;
        else if (k == "downloadBtn") g_cfg.downloadBtn = atoi(v.c_str()) != 0;
    }
    if (g_cfg.codec != 0 && g_cfg.codec != 7 && g_cfg.codec != 12 && g_cfg.codec != 13)
        g_cfg.codec = 7;
    Log("config: codec=%d blockLogin=%d autostart=%d startupAnim=%d webAnim=%d chatPanel=%d downloadBtn=%d",
        g_cfg.codec, g_cfg.blockLogin ? 1 : 0, g_cfg.autostart ? 1 : 0,
        g_cfg.startupAnim ? 1 : 0, g_cfg.webAnim ? 1 : 0,
        g_cfg.chatPanel ? 1 : 0, g_cfg.downloadBtn ? 1 : 0);
}

void saveConfig() {
    std::ofstream f((g_dir + L"\\config.ini").c_str(), std::ios::trunc);
    if (!f) return;
    f << "codec=" << g_cfg.codec << "\n"
      << "blockLogin=" << (g_cfg.blockLogin ? 1 : 0) << "\n"
      << "autostart=" << (g_cfg.autostart ? 1 : 0) << "\n"
      << "startupAnim=" << (g_cfg.startupAnim ? 1 : 0) << "\n"
      << "webAnim=" << (g_cfg.webAnim ? 1 : 0) << "\n"
      << "chatPanel=" << (g_cfg.chatPanel ? 1 : 0) << "\n"
      << "downloadBtn=" << (g_cfg.downloadBtn ? 1 : 0) << "\n";
}

// 本地转码参数单独存 local.ini，避免和网页改写的 codec 互相覆盖
static void LoadLocal() {
    int codec = 7, level = 40;
    bool base = false;
    std::ifstream f((g_dir + L"\\local.ini").c_str());
    if (f) {
        std::string line;
        while (std::getline(f, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = trim(line.substr(0, eq));
            std::string v = trim(line.substr(eq + 1));
            if (k == "codec") codec = atoi(v.c_str());
            else if (k == "level") level = atoi(v.c_str());
            else if (k == "baseline") base = atoi(v.c_str()) != 0;
        }
    }
    if (codec != 7 && codec != 12 && codec != 13) codec = 7;
    if (level != 0 && level != 40 && level != 41 && level != 50) level = 40;
    TranscodeSetCfg(codec, level, base);
}

void loadLocalCfg() { LoadLocal(); }

void saveLocalCfg(int codec, int level, bool baseline) {
    std::ofstream f((g_dir + L"\\local.ini").c_str(), std::ios::trunc);
    if (!f) return;
    f << "codec=" << codec << "\n"
      << "level=" << level << "\n"
      << "baseline=" << (baseline ? 1 : 0) << "\n";
}


void MigrateOldData() {
    wchar_t appdata[MAX_PATH] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, MAX_PATH);
    std::wstring newCerts = g_dir + L"\\certs";
    if (fileExists(newCerts + L"\\ca.pem")) return;
    std::wstring oldDir = std::wstring(appdata) + L"\\BiliProxy";
    const wchar_t* files[] = { L"ca.pem", L"ca.key", L"leaf.pem", L"leaf.key", L"ca.cer" };
    for (auto f : files) {
        std::wstring src = oldDir + L"\\certs\\" + f;
        if (fileExists(src)) CopyFileW(src.c_str(), (newCerts + L"\\" + f).c_str(), FALSE);
    }
}

void RemoveAllAutostart() {
    removeAutostart();
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        RegDeleteKeyValueW(k, nullptr, L"BiliProxy");
        RegCloseKey(k);
    }
}
