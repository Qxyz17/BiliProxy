#include "common.h"

const int      kPort     = 18080;
const char*    kListen   = "127.0.0.1";
const wchar_t* kRunValue = L"BiliProxy";
const wchar_t* kCaSubject = L"BiliProxy Local Root CA";
const char*    kCaSubjectU8 = "BiliProxy Local Root CA";

Settings      g_cfg;
std::wstring  g_dir;
std::string   g_dirU8;
unsigned long g_pacVersion = 1;
LONG          g_rewrites = 0;
SSL_CTX*      g_srvCtx = nullptr;
SSL_CTX*      g_cliCtx = nullptr;
std::mutex    g_logMu;
FILE*         g_logFile = nullptr;
bool          g_verbose = false;
SOCKET        g_listenSock = INVALID_SOCKET;

std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMu);
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[4300];
    snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d:%02d.%03d %s\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
    if (g_logFile) {
        fputs(line, g_logFile);
        fflush(g_logFile);
    }
    if (g_verbose) fputs(line, stderr);
}

void LogSslErr(const char* where) {
    unsigned long e = ERR_get_error();
    if (e) {
        char b[256];
        ERR_error_string_n(e, b, sizeof b);
        Log("%s: %s", where, b);
        ERR_clear_error();
    }
}

std::string readFile(const std::wstring& p) {
    std::ifstream f(p.c_str(), std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFile(const std::wstring& p, const std::string& d) {
    std::ofstream f(p.c_str(), std::ios::binary);
    if (!f) return false;
    f.write(d.data(), (std::streamsize)d.size());
    return f.good();
}
