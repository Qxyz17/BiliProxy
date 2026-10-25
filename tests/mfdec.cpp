// Media Foundation 解码测试：模拟桌面播放器 / 希沃白板5 的解码路径
// 用法: mfdec.exe <file> [全零=软解优先]
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <cstdio>
#include <string>

static std::string HrStr(HRESULT hr) {
    char b[64];
    sprintf(b, "0x%08X", (unsigned)hr);
    switch (hr) {
        case MF_E_INVALIDMEDIATYPE: return b + std::string(" MF_E_INVALIDMEDIATYPE");
        case MF_E_TOPO_CODEC_NOT_FOUND: return b + std::string(" MF_E_TOPO_CODEC_NOT_FOUND");
        case MF_E_UNSUPPORTED_FORMAT: return b + std::string(" MF_E_UNSUPPORTED_FORMAT");
        case MF_E_INVALID_STREAM_DATA: return b + std::string(" MF_E_INVALID_STREAM_DATA");
        case MF_E_NO_MORE_TYPES: return b + std::string(" MF_E_NO_MORE_TYPES");
        case MF_E_TRANSFORM_TYPE_NOT_SET: return b + std::string(" MF_E_TRANSFORM_TYPE_NOT_SET");
        default: return b;
    }
}

static void PrintType(IMFMediaType* t) {
    GUID sub = GUID_NULL;
    UINT32 w = 0, h = 0;
    t->GetGUID(MF_MT_SUBTYPE, &sub);
    MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
    printf("    subtype=%08lX-%04hX  %ux%u\n",
           (unsigned long)sub.Data1, (unsigned short)sub.Data2, w, h);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { wprintf(L"usage: mfdec <file>\n"); return 2; }
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) { printf("MFStartup %s\n", HrStr(hr).c_str()); return 1; }

    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 1);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1);

    IMFSourceReader* rd = nullptr;
    hr = MFCreateSourceReaderFromURL(argv[1], attrs, &rd);
    if (FAILED(hr)) { printf("SourceReader open: %s\n", HrStr(hr).c_str()); return 1; }

    // 报告源类型
    IMFMediaType* cur = nullptr;
    if (SUCCEEDED(rd->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur)) && cur) {
        printf("source video type:\n");
        PrintType(cur);
        cur->Release();
    }

    // 只取视频
    rd->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    rd->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    // 请求 RGB32 输出（让 MF 自选解码器 + 视频处理器）
    IMFMediaType* out = nullptr;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = rd->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, out);
    out->Release();
    if (FAILED(hr)) { printf("SetOutputType RGB32: %s\n", HrStr(hr).c_str()); return 1; }

    // 打印实际选中的解码输出类型（可看出是否 DXVA）
    IMFMediaType* got = nullptr;
    if (SUCCEEDED(rd->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &got)) && got) {
        printf("decoded output type:\n");
        PrintType(got);
        got->Release();
    }

    int frames = 0;
    for (int i = 0; i < 60; ++i) {
        DWORD flags = 0, idx = 0;
        LONGLONG ts = 0;
        IMFSample* smp = nullptr;
        hr = rd->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &flags, &ts, &smp);
        if (smp) smp->Release();
        if (FAILED(hr)) { printf("ReadSample: %s\n", HrStr(hr).c_str()); break; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { printf("EOS after %d frames\n", frames); break; }
        if (flags & MF_SOURCE_READERF_ERROR) { printf("READER ERROR at frame %d\n", frames); break; }
        if (flags & MF_SOURCE_READERF_STREAMTICK) continue;
        ++frames;
    }
    printf("decoded %d frames, result=%s\n", frames, frames > 0 ? "OK" : "FAILED");

    rd->Release();
    attrs->Release();
    MFShutdown();
    return frames > 0 ? 0 : 1;
}
