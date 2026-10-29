#include "common.h"
#include "transcode.h"
#include "ffmpeg_embed.h"

#include <filesystem>
#include <cctype>

namespace fs = std::filesystem;

static CRITICAL_SECTION g_cs;
static bool             g_csInit = false;
static std::vector<TJob> g_jobs;
static HANDLE           g_evt = nullptr;
static HANDLE           g_thr = nullptr;
static std::wstring     g_ffmpeg, g_errFile, g_lastOutDir;
static TCfg             g_tc;

static void Lock()   { EnterCriticalSection(&g_cs); }
static void Unlock() { LeaveCriticalSection(&g_cs); }

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    if (!n) return L".";
    std::wstring s(buf, n);
    size_t p = s.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"." : s.substr(0, p);
}

static std::wstring Sidecar(const wchar_t* name) {
    std::wstring p = ExeDir() + L"\\" + name;
    if (fileExists(p)) return p;
    wchar_t found[MAX_PATH * 4];
    if (SearchPathW(nullptr, name, nullptr, MAX_PATH * 4, found, nullptr)) return found;
    return L"";
}

// ------------------------------------------------------------------ 进程

struct RunResult {
    int  code = -1;
    std::string out;
};

// jobIdx >= 0 时解析 -progress 输出并回写百分比；durSec>0 用于换算
static RunResult RunTool(const std::wstring& exe, const std::wstring& args,
                         int jobIdx, double durSec) {
    RunResult r;

    SECURITY_ATTRIBUTES sa{ sizeof sa, nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return r;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    HANDLE errH = CreateFileW(g_errFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE nulH = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError  = (errH != INVALID_HANDLE_VALUE) ? errH : wr;
    si.hStdInput  = (nulH != INVALID_HANDLE_VALUE) ? nulH : INVALID_HANDLE_VALUE;

    std::wstring cmdline = L"\"" + exe + L"\" " + args;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exe.c_str(), &cmdline[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (errH != INVALID_HANDLE_VALUE) CloseHandle(errH);
    if (nulH != INVALID_HANDLE_VALUE) CloseHandle(nulH);
    if (!ok) { CloseHandle(rd); return r; }

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
            if (!line.empty() && line.back() == '\r') line.pop_back();
            r.out += line;
            r.out += '\n';
            if (jobIdx >= 0 && durSec > 0.05 && line.compare(0, 12, "out_time_us=") == 0) {
                long long us = _strtoi64(line.c_str() + 12, nullptr, 10);
                int pct = (int)(us / (durSec * 1e4));
                if (pct < 0) pct = 0;
                if (pct > 99) pct = 99;
                Lock();
                if (jobIdx < (int)g_jobs.size() && g_jobs[jobIdx].state == 2)
                    g_jobs[jobIdx].percent = pct;
                Unlock();
            }
        }
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &ec);
    r.code = (int)ec;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return r;
}

static std::string ReadErrTail() {
    std::string all = readFile(g_errFile);
    size_t p = all.find_last_not_of("\r\n ");
    if (p == std::string::npos) return {};
    size_t b = all.find_last_of('\n', p);
    size_t st = (b == std::string::npos) ? 0 : b + 1;
    return all.substr(st, p - st + 1);
}

// ------------------------------------------------------------------ 探测

struct Probe {
    std::string vcodec, vpix, acodec;
    int w = 0, h = 0;
    double dur = 0;
    int profile = -1;   // H.264 profile_idc（66=baseline 77=main 100=high）
    int level   = -1;   // H.264 level_idc（30/31/40/41/50/51）
};

// 直接扫 MP4 的 avcC 盒取 profile/level：比让 ffmpeg 解析 SPS 快得多
static void ReadAvcProfile(const std::wstring& in, Probe* pr) {
    FILE* f = _wfopen(in.c_str(), L"rb");
    if (!f) return;
    std::vector<unsigned char> buf(1 << 16);
    size_t got = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (got < 64) return;
    for (size_t i = 0; i + 12 < got; ++i) {
        if (buf[i] == 'a' && buf[i + 1] == 'v' && buf[i + 2] == 'c' && buf[i + 3] == 'C') {
            pr->profile = buf[i + 5];
            pr->level   = buf[i + 7];
            return;
        }
    }
}

static bool LooksLikePixFmt(const std::string& s) {
    static const char* kPre[] = { "yuv", "yuva", "nv12", "nv21", "p010", "p016", "p210",
                                  "rgb", "bgr", "gbr", "gray", "pal8", "xyz12" };
    for (auto p : kPre) {
        size_t n = strlen(p);
        if (s.size() >= n && s.compare(0, n, p) == 0) return true;
    }
    return false;
}

static Probe ProbeFile(const std::wstring& in) {
    Probe pr;
    RunTool(g_ffmpeg, L"-hide_banner -nostdin -i \"" + in + L"\"", -1, 0);
    std::string txt = readFile(g_errFile);

    size_t d = txt.find("Duration: ");
    if (d != std::string::npos) {
        int hh = 0, mm = 0;
        double ss = 0;
        if (sscanf(txt.c_str() + d + 10, "%d:%d:%lf", &hh, &mm, &ss) == 3)
            pr.dur = hh * 3600.0 + mm * 60.0 + ss;
    }

    size_t vl = txt.find("Video: ");
    if (vl != std::string::npos) {
        size_t e = txt.find('\n', vl);
        std::string line = txt.substr(vl + 7, e == std::string::npos ? std::string::npos : e - vl - 7);
        size_t sp = line.find_first_of(" (,");
        pr.vcodec = line.substr(0, sp == std::string::npos ? line.size() : sp);

        // 像素格式：按逗号分段，取第一个像 pix_fmt 的段
        size_t pos = 0;
        while (pos < line.size()) {
            size_t c = line.find(',', pos);
            std::string seg = line.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
            size_t st = seg.find_first_not_of(' ');
            if (st != std::string::npos) {
                size_t pe = seg.find_first_of("(,", st);
                std::string tok = seg.substr(st, pe == std::string::npos ? std::string::npos : pe - st);
                while (!tok.empty() && tok.back() == ' ') tok.pop_back();
                if (LooksLikePixFmt(tok)) { pr.vpix = tok; break; }
            }
            if (c == std::string::npos) break;
            pos = c + 1;
        }

        // 分辨率：数字x数字（避开 0x31637661 这类十六进制）
        for (size_t i = 1; i + 1 < line.size(); ++i) {
            if (line[i] != 'x' || !isdigit((unsigned char)line[i + 1])) continue;
            size_t l = i, r = i + 1;
            while (l > 0 && isdigit((unsigned char)line[l - 1])) --l;
            while (r < line.size() && isdigit((unsigned char)line[r])) ++r;
            size_t lw = i - l, rw = r - i - 1;
            if (lw < 2 || lw > 5 || rw < 2 || rw > 5) continue;
            int w = atoi(line.substr(l, lw).c_str());
            int h = atoi(line.substr(i + 1, rw).c_str());
            if (w >= 64 && h >= 64) { pr.w = w; pr.h = h; break; }
        }
    }

    size_t al = txt.find("Audio: ");
    if (al != std::string::npos) {
        size_t e = txt.find('\n', al);
        std::string line = txt.substr(al + 7, e == std::string::npos ? std::string::npos : e - al - 7);
        size_t sp = line.find_first_of(" (,");
        pr.acodec = line.substr(0, sp == std::string::npos ? line.size() : sp);
    }
    if (pr.vcodec == "h264") ReadAvcProfile(in, &pr);
    return pr;
}

static std::wstring CodecTag(int codec) {
    if (codec == 12) return L"h265";
    if (codec == 13) return L"av1";
    return L"h264";
}

static std::wstring MakeOutPath(const std::wstring& in, int codec) {
    fs::path p(in);
    fs::path dir = p.parent_path();
    std::wstring stem = p.stem().wstring();
    std::wstring tag = CodecTag(codec);
    for (int n = 1; n < 100; ++n) {
        std::wstring name = stem + (n == 1 ? (L"." + tag + L".mp4")
                                           : (L"." + tag + L"." + std::to_wstring(n) + L".mp4"));
        fs::path cand = dir / name;
        if (!fs::exists(cand)) return cand.wstring();
    }
    return (dir / (stem + L"." + tag + L".mp4")).wstring();
}

static void SetJob(int idx, int state, const std::wstring& note = L"") {
    Lock();
    if (idx < (int)g_jobs.size()) {
        g_jobs[idx].state = state;
        if (!note.empty() || state == 3) g_jobs[idx].note = note;
        if (state == 3) g_jobs[idx].percent = 100;
    }
    Unlock();
}

// ------------------------------------------------------------------ 单个任务

static void ProcessJob(int idx) {
    std::wstring in;
    Lock();
    in = g_jobs[idx].path;
    g_jobs[idx].state = 1;
    Unlock();

    if (!fs::exists(in)) { SetJob(idx, 5, L"文件不存在"); return; }

    Probe pr = ProbeFile(in);
    if (pr.vcodec.empty()) { SetJob(idx, 5, L"没有视频流"); return; }

    TCfg tc;
    Lock();
    tc = g_tc;
    Unlock();

    // 源本来就是这个编码、而且分辨率/像素格式/AAC 都合规时跳过
    bool srcMatch = false;
    if (tc.codec == 7)  srcMatch = (pr.vcodec == "h264");
    if (tc.codec == 12) srcMatch = (pr.vcodec == "hevc");
    if (tc.codec == 13) srcMatch = (pr.vcodec == "av1");

    bool already = srcMatch && pr.w <= 1920 && pr.h <= 1200 &&
                   (pr.vpix.empty() || pr.vpix == "yuv420p") &&
                   (pr.acodec.empty() || pr.acodec == "aac");

    // 编码名对不代表能放：profile/level 过高（如 High@5.1）老硬解直接拒解。
    // 只有明确低于目标等级才算“已经是目标编码”，否则照样重编。
    if (already && tc.codec == 7 && pr.profile >= 0 && pr.level >= 0) {
        int capProfile = tc.baseline ? 66 : 100;                 // baseline 只认 66；否则 High 也行
        int capLevel   = tc.baseline ? 31 : (tc.level > 0 ? tc.level : 40);
        if (pr.profile > capProfile || pr.level > capLevel) {
            already = false;
            Log("retranscode: source High@%d exceeds target profile=%d level=%d",
                pr.level, capProfile, capLevel);
        }
    }
    if (already) { SetJob(idx, 4, L"已是目标编码，无需转换"); return; }

    std::wstring out = MakeOutPath(in, tc.codec);
    Lock();
    g_jobs[idx].out = out;
    Unlock();

    std::wstring audioArgs = pr.acodec.empty() ? L" -an"
                           : (pr.acodec == "aac" ? L" -c:a copy" : L" -c:a aac -b:a 192k -ac 2");

    // ---- 编码器参数：关键是 profile / level，播放器硬解认不认全看这里 ----
    std::wstring venc, prof;
    if (tc.codec == 12) {
        venc = L"libx265 -preset fast -crf 24 -pix_fmt yuv420p";
        // 老 GPU 的 HEVC 硬解一般只到 Main / Main10 @ L4；Main 最保险
        prof = L" -profile:v main -level:v 4.0";
    } else if (tc.codec == 13) {
        // 内置 ffmpeg 用 libaom-av1（无 libsvtav1）
        venc = L"libaom-av1 -cpu-used 8 -crf 30 -row-mt 1 -pix_fmt yuv420p";
        prof = L"";
    } else {
        // 默认走 baseline：无 B 帧、无 8x8 变换、无 CABAC，
        // 兆芯 C-960 / 老播放器 / 希沃白板5 的硬解全都能吃
        venc = L"libx264 -preset veryfast -crf 20 -pix_fmt yuv420p";
        if (tc.baseline) {
            prof = L" -profile:v baseline -level:v 3.1";
        } else {
            prof = L" -profile:v high";
            wchar_t lv[32] = L"";
            if (tc.level == 40) swprintf(lv, 32, L" -level:v 4.0");
            else if (tc.level == 41) swprintf(lv, 32, L" -level:v 4.1");
            else if (tc.level == 50) swprintf(lv, 32, L" -level:v 5.0");
            prof += lv;
            // 4.0 及以下关掉 8x8 变换，兼容性更好
            if (tc.level > 0 && tc.level <= 41) prof += L" -x264-params transform_8x8=0:cabac=1";
        }
    }

    std::wstring args =
        L"-y -hide_banner -nostdin -i \"" + in + L"\"" +
        L" -map 0:v:0" + (pr.acodec.empty() ? std::wstring(L"") : std::wstring(L" -map 0:a:0")) +
        L" -vf \"scale='min(1920,iw)':-2,fps=30\""
        L" -c:v " + venc + prof +
        audioArgs +
        L" -movflags +faststart -progress pipe:1 -nostats \"" + out + L"\"";

    Log("transcode start (%s %dx%d -> codec %d level %d base %d) %s",
        pr.vcodec.c_str(), pr.w, pr.h, tc.codec, tc.level, tc.baseline ? 1 : 0, U8(in).c_str());
    SetJob(idx, 2, L"转码中…");

    RunResult r = RunTool(g_ffmpeg, args, idx, pr.dur);
    if (r.code == 0 && fs::exists(out)) {
        Lock();
        g_lastOutDir = fs::path(out).parent_path().wstring();
        Unlock();
        SetJob(idx, 3, L"完成");
        Log("transcode done %s", U8(out).c_str());
    } else {
        std::string tail = ReadErrTail();
        if (tail.empty()) tail = "ffmpeg 退出码 " + std::to_string(r.code);
        SetJob(idx, 5, W(tail));
        Log("transcode failed: %s", tail.c_str());
        std::error_code ec;
        fs::remove(out, ec);
    }
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

// ------------------------------------------------------------------ 对外

void TranscodeInit() {
    if (g_csInit) return;
    InitializeCriticalSection(&g_cs);
    g_csInit = true;
    g_errFile = g_dir + L"\\ffmpeg-err.txt";

    // 优先用 exe 内嵌的 ffmpeg；没有才退回同目录/安装目录的 ffmpeg.exe
    g_ffmpeg = EnsureEmbeddedFfmpeg();
    if (g_ffmpeg.empty()) g_ffmpeg = Sidecar(L"ffmpeg.exe");
    if (g_ffmpeg.empty())
        Log("transcode: no ffmpeg available (%s)", U8(ExeDir()).c_str());
    else
        Log("transcode: ffmpeg = %ls", g_ffmpeg.c_str());

    g_evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_thr = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
}

bool TranscodeToolsPresent() { return !g_ffmpeg.empty(); }

std::wstring TranscodeFfmpegPath() { return g_ffmpeg; }

void TranscodeSetCfg(int codec, int level, bool baseline) {
    if (!g_csInit) return;
    Lock();
    if (codec == 7 || codec == 12 || codec == 13) g_tc.codec = codec;
    if (level == 0 || level == 40 || level == 41 || level == 50) g_tc.level = level;
    g_tc.baseline = baseline;
    Unlock();
}

void TranscodeGetCfg(int* codec, int* level, bool* baseline) {
    TCfg c;
    if (g_csInit) { Lock(); c = g_tc; Unlock(); }
    if (codec) *codec = c.codec;
    if (level) *level = c.level;
    if (baseline) *baseline = c.baseline;
}

void TranscodeEnqueue(const std::vector<std::wstring>& files) {
    if (!g_csInit || !TranscodeToolsPresent()) return;
    Lock();
    for (auto& f : files) {
        bool dup = false;
        for (auto& j : g_jobs)
            if (j.path == f && j.state < 3) { dup = true; break; }
        if (dup) continue;
        TJob j;
        j.path = f;
        g_jobs.push_back(j);
    }
    Unlock();
    SetEvent(g_evt);
}

int TranscodeCount() {
    if (!g_csInit) return 0;
    Lock();
    int n = (int)g_jobs.size();
    Unlock();
    return n;
}

bool TranscodeGet(int i, TJob* out) {
    if (!g_csInit) return false;
    Lock();
    bool ok = i >= 0 && i < (int)g_jobs.size();
    if (ok) *out = g_jobs[i];
    Unlock();
    return ok;
}

bool TranscodeActive() {
    if (!g_csInit) return false;
    Lock();
    bool a = false;
    for (auto& j : g_jobs) if (j.state < 3) { a = true; break; }
    Unlock();
    return a;
}

void TranscodeClearDone() {
    if (!g_csInit) return;
    Lock();
    g_jobs.erase(std::remove_if(g_jobs.begin(), g_jobs.end(),
                                [](const TJob& j) { return j.state >= 3; }),
                 g_jobs.end());
    Unlock();
}

std::wstring TranscodeLastOutDir() {
    if (!g_csInit) return L"";
    Lock();
    std::wstring d = g_lastOutDir;
    Unlock();
    return d;
}
