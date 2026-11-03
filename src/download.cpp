#include "common.h"
#include "download.h"
#include "transcode.h"

#include <filesystem>
#include <deque>

namespace fs = std::filesystem;

// 缓存的 playurl 里取出的流地址
struct Streams {
    std::string vurl, aurl;
    int w = 0, h = 0;
    double dur = 0;
    std::wstring title;
    bool ok = false;
};

static CRITICAL_SECTION g_cs;
static bool g_csInit = false;
static Streams g_last;
static std::vector<DlJob> g_jobs;
static HANDLE g_evt = nullptr;
static std::wstring g_outDir, g_ffmpeg, g_errFile;

static void Lock() { EnterCriticalSection(&g_cs); }
static void Unlock() { LeaveCriticalSection(&g_cs); }

static void SetJob(int idx, int state, int percent, const std::wstring& note = L"");
static int  RunFfmpegProgress(const std::wstring& args, int idx, double durSec);

static std::string JsonStr(const std::string& obj, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = obj.find(k);
    if (p == std::string::npos) return {};
    p = obj.find(':', p + k.size());
    if (p == std::string::npos) return {};
    size_t q = obj.find('"', p);
    if (q == std::string::npos) return {};
    std::string v;
    for (size_t i = q + 1; i < obj.size(); ++i) {
        if (obj[i] == '\\' && i + 1 < obj.size()) { v += obj[i + 1]; ++i; continue; }
        if (obj[i] == '"') break;
        v += obj[i];
    }
    return v;
}

static int JsonInt(const std::string& obj, const char* key, int def = 0) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = obj.find(k);
    if (p == std::string::npos) return def;
    p = obj.find(':', p + k.size());
    if (p == std::string::npos) return def;
    return atoi(obj.c_str() + p + 1);
}

// 在数组里挑带宽最大的对象
static std::string BestInArray(const std::string& body, const char* key, int* outW, int* outH) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = body.find(k);
    if (p == std::string::npos) return {};
    size_t lb = body.find('[', p);
    if (lb == std::string::npos) return {};
    size_t rb = std::string::npos;
    int depth = 0;
    bool instr = false;
    for (size_t i = lb + 1; i < body.size(); ++i) {
        char c = body[i];
        if (instr) { if (c == '\\') ++i; else if (c == '"') instr = false; continue; }
        if (c == '"') instr = true;
        else if (c == '[' || c == '{') ++depth;
        else if (c == ']' || c == '}') { if (depth == 0) { rb = i; break; } --depth; }
    }
    if (rb == std::string::npos) return {};

    std::string best;
    int bestBw = -1, bw = 0, w = 0, h = 0;
    size_t i = lb + 1;
    while (i < rb) {
        while (i < rb && body[i] != '{') ++i;
        if (i >= rb) break;
        size_t start = i;
        int d = 0;
        bool s = false;
        for (; i < rb; ++i) {
            char c = body[i];
            if (s) { if (c == '\\') ++i; else if (c == '"') s = false; continue; }
            if (c == '"') s = true;
            else if (c == '{') ++d;
            else if (c == '}') { --d; if (d == 0) { ++i; break; } }
        }
        std::string obj = body.substr(start, i - start);
        bw = JsonInt(obj, "bandwidth");
        if (bw > bestBw) {
            std::string u = JsonStr(obj, "baseUrl");
            if (u.empty()) u = JsonStr(obj, "base_url");
            if (!u.empty()) { bestBw = bw; best = u; w = JsonInt(obj, "width"); h = JsonInt(obj, "height"); }
        }
    }
    if (outW) *outW = w;
    if (outH) *outH = h;
    return best;
}

void DlCachePlayurl(const std::string& body) {
    if (!g_csInit) return;
    int w = 0, h = 0;
    std::string v = BestInArray(body, "video", &w, &h);
    std::string a = BestInArray(body, "audio", nullptr, nullptr);
    if (v.empty()) return;
    Streams s;
    s.vurl = v;
    s.aurl = a;
    s.w = w;
    s.h = h;
    s.dur = atof(JsonStr(body, "duration").c_str());
    s.ok = true;
    Lock();
    g_last = s;
    Unlock();
}

static std::wstring Sanitize(const std::wstring& t) {
    std::wstring r;
    for (wchar_t c : t) {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|') r += L'_';
        else r += c;
        if (r.size() >= 80) break;
    }
    while (!r.empty() && r.back() == L' ') r.pop_back();
    return r.empty() ? L"video" : r;
}

static void SetJob(int idx, int state, int percent, const std::wstring& note) {
    Lock();
    if (idx >= 0 && idx < (int)g_jobs.size()) {
        g_jobs[idx].state = state;
        g_jobs[idx].percent = percent;
        if (!note.empty()) g_jobs[idx].note = note;
    }
    Unlock();
}

static void ProcessJob(int idx) {
    Streams s;
    Lock();
    s = g_last;
    Unlock();
    if (!s.ok) { SetJob(idx, 3, 0, L"没有可用的播放地址"); return; }

    std::wstring base = Sanitize(s.title.empty() ? L"video" : s.title);
    std::wstring out = g_outDir + L"\\" + base + L".mp4";
    for (int n = 2; fs::exists(out) && n < 100; ++n)
        out = g_outDir + L"\\" + base + L"(" + std::to_wstring(n) + L").mp4";
    Lock();
    g_jobs[idx].out = out;
    Unlock();

    std::wstring ref = L"Referer: https://www.bilibili.com/\r\nUser-Agent: Mozilla/5.0\r\n";
    std::wstring args = L"-y -hide_banner -nostdin "
                        L"-headers \"" + ref + L"\" -i \"" + W(s.vurl) + L"\" ";
    if (!s.aurl.empty())
        args += L"-headers \"" + ref + L"\" -i \"" + W(s.aurl) + L"\" ";

    if (g_jobs[idx].wm) {
        // 有水印：烧录 bilibili 角标，视频必须重编
        args += L"-map 0:v:0" + std::wstring(s.aurl.empty() ? L"" : L" -map 1:a:0") +
                L" -vf \"drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='bilibili':"
                L"fontsize=h/32:fontcolor=white@0.55:x=w-tw-h/64:y=h/64\""
                L" -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p" +
                (s.aurl.empty() ? L" -an" : L" -c:a aac -b:a 192k");
    } else {
        // 无水印：流直拷
        args += L"-map 0:v:0" + std::wstring(s.aurl.empty() ? L"" : L" -map 1:a:0") +
                L" -c copy";
    }
    args += L" -movflags +faststart -progress pipe:1 -nostats \"" + out + L"\"";

    Log("download start (wm=%d %dx%d) %s", g_jobs[idx].wm, s.w, s.h, s.vurl.c_str());
    SetJob(idx, 1, 0, L"下载中");

    int code = RunFfmpegProgress(args, idx, s.dur);
    if (code == 0 && fs::exists(out))
        SetJob(idx, 2, 100, L"完成");
    else {
        std::error_code ec;
        fs::remove(out, ec);
        SetJob(idx, 3, 0, L"下载失败");
    }
}

static int RunFfmpegProgress(const std::wstring& args, int idx, double durSec) {
    SECURITY_ATTRIBUTES sa{ sizeof sa, nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    HANDLE errH = CreateFileW(g_errFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE nulH = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = (errH != INVALID_HANDLE_VALUE) ? errH : wr;
    si.hStdInput = (nulH != INVALID_HANDLE_VALUE) ? nulH : INVALID_HANDLE_VALUE;

    std::wstring cmdline = L"\"" + g_ffmpeg + L"\" " + args;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(g_ffmpeg.c_str(), &cmdline[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (errH != INVALID_HANDLE_VALUE) CloseHandle(errH);
    if (nulH != INVALID_HANDLE_VALUE) CloseHandle(nulH);
    if (!ok) { CloseHandle(rd); return -1; }

    std::string buf, line;
    char chunk[4096];
    DWORD got = 0;
    for (;;) {
        if (!ReadFile(rd, chunk, sizeof chunk, &got, nullptr) || got == 0) break;
        buf.append(chunk, got);
        size_t p;
        while ((p = buf.find('\n')) != std::string::npos) {
            line.assign(buf, 0, p);
            buf.erase(0, p + 1);
            if (line.compare(0, 12, "out_time_us=") == 0 && durSec > 0.05) {
                long long us = _strtoi64(line.c_str() + 12, nullptr, 10);
                int pct = (int)(us / (durSec * 1e4));
                if (pct < 0) pct = 0;
                if (pct > 99) pct = 99;
                SetJob(idx, 1, pct);
            }
        }
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)ec;
}

static DWORD WINAPI WorkerProc(LPVOID) {
    for (;;) {
        WaitForSingleObject(g_evt, INFINITE);
        for (;;) {
            int idx = -1;
            Lock();
            for (size_t i = 0; i < g_jobs.size(); ++i)
                if (g_jobs[i].state == 0) { idx = (int)i; g_jobs[i].state = 1; break; }
            Unlock();
            if (idx < 0) break;
            ProcessJob(idx);
        }
    }
    return 0;
}

void DlInit() {
    if (g_csInit) return;
    InitializeCriticalSection(&g_cs);
    g_csInit = true;
    wchar_t up[MAX_PATH] = {};
    GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
    g_outDir = std::wstring(up) + L"\\Downloads\\BiliProxy";
    CreateDirectoryW(g_outDir.c_str(), nullptr);
    g_errFile = g_dir + L"\\dl-err.txt";
    g_ffmpeg = TranscodeFfmpegPath();
    g_evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
}

int DlStart(int wm, const std::string& titleU8, std::wstring& err) {
    if (!g_csInit) { err = L"未初始化"; return -1; }
    Lock();
    if (!g_last.ok) { Unlock(); err = L"先播放一下视频"; return -1; }
    if (!titleU8.empty()) g_last.title = W(titleU8);
    DlJob j;
    j.wm = wm;
    g_jobs.push_back(j);
    int id = (int)g_jobs.size() - 1;
    Unlock();
    SetEvent(g_evt);
    return id;
}

bool DlGet(int id, DlJob* out) {
    if (!g_csInit) return false;
    Lock();
    bool ok = id >= 0 && id < (int)g_jobs.size();
    if (ok) *out = g_jobs[id];
    Unlock();
    return ok;
}

std::wstring DlOutDir() { return g_outDir; }
