// 内置 ffmpeg：压缩二进制直接从 exe 的 .rdata 里取出，解压到 %LOCALAPPDATA%\BiliProxy\ffmpeg.exe
#include "common.h"
#include "ffmpeg_embed.h"

#include <zlib.h>

// objcopy 生成的符号（objcopy 在 build 目录里执行，文件名 ffmpeg.deflate）
extern "C" {
    extern const unsigned char _binary_ffmpeg_deflate_start[];
    extern const unsigned char _binary_ffmpeg_deflate_end[];
}

std::wstring EnsureEmbeddedFfmpeg() {
    std::wstring target = g_dir + L"\\ffmpeg.exe";

    const unsigned char* b = _binary_ffmpeg_deflate_start;
    size_t n = (size_t)(_binary_ffmpeg_deflate_end - _binary_ffmpeg_deflate_start);
    if (n == 0) { Log("embed: no blob"); return L""; }

    // 解压到临时文件，成功后改名（避免半截文件被当成可用）
    std::wstring tmp = g_dir + L"\\ffmpeg.tmp";
    std::string comp((const char*)b, n);

    // 先解到一个内存块
    std::vector<unsigned char> out;
    out.reserve(120u << 20);
    uLongf cap = 0;
    {
        // 逐块 inflate，边解边追加，避免一次性预估
        z_stream zs{};
        if (inflateInit2(&zs, -15) != Z_OK) { Log("embed: inflateInit failed"); return L""; }
        const size_t step = 4096;
        unsigned char buf[step];
        size_t pos = 0;
        int rc = Z_OK;
        while (rc != Z_STREAM_END) {
            if (zs.avail_in == 0) {
                size_t take = (n - pos) < (1u << 20) ? (n - pos) : (1u << 20);
                zs.next_in = (Bytef*)(comp.data() + pos);
                zs.avail_in = (uInt)take;
                pos += take;
            }
            zs.next_out = buf;
            zs.avail_out = (uInt)step;
            rc = inflate(&zs, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) { inflateEnd(&zs); Log("embed: inflate rc=%d", rc); return L""; }
            size_t got = step - zs.avail_out;
            out.insert(out.end(), buf, buf + got);
        }
        cap = (uLongf)out.size();
        inflateEnd(&zs);
    }

    if (cap < 1000000) { Log("embed: blob too small (%zu)", out.size()); return L""; }

    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) { Log("embed: cannot write %ls", tmp.c_str()); return L""; }
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);

    // 已有可用文件且大小对得上就跳过
    if (fileExists(target)) {
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        if (GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &fa) &&
            fa.nFileSizeLow == (DWORD)out.size() && fa.nFileSizeHigh == 0) {
            DeleteFileW(tmp.c_str());
            Log("embed: ffmpeg already extracted (%zu bytes)", out.size());
            return target;
        }
    }

    DeleteFileW(target.c_str());
    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        Log("embed: move failed %lu", GetLastError());
        DeleteFileW(tmp.c_str());
        return L"";
    }
    Log("embed: extracted ffmpeg.exe (%zu bytes)", out.size());
    return target;
}
