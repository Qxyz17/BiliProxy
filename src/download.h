#pragma once

#include "common.h"

// 网页下载：从缓存的 playurl 里取 dash 流，用内置 ffmpeg 抓取并合成 mp4
// state: 0=排队 1=下载中 2=完成 3=失败

struct DlJob {
    int          state   = 0;
    int          percent = 0;
    int          wm      = 0;
    std::wstring out;
    std::wstring note;
};

void DlInit();
void DlCachePlayurl(const std::string& body, const std::string& query);
int  DlStart(int wm, const std::string& titleU8, const std::string& aid,
             const std::string& cid, std::wstring& err);
bool DlGet(int id, DlJob* out);
// 直接从缓存的流里抽帧，不碰页面播放器；每帧是 JPEG 的 base64
bool DlFrames(const std::string& cid, int count, int width,
              std::vector<std::string>& frames, std::wstring& err);
std::wstring DlOutDir();
