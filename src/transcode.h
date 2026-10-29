#pragma once

#include <string>
#include <vector>

// 本地视频转码队列
// 目标编码 tcCodec: 7=H.264(AVC) 12=H.265(HEVC) 13=AV1
// 兼容等级 tcLevel: 用于解决“编码对但播放器硬解不认”的问题（profile/level 过高）
//   level 0 = 自动（按分辨率给最保守的等级）
//   level 40 / 41 / 51 = 强制 Baseline/Main/High profile + Level 4.0/4.1/5.1
// state: 0=排队 1=分析 2=转码 3=完成 4=跳过 5=失败

struct TJob {
    std::wstring path;
    std::wstring out;
    int          state   = 0;
    int          percent = 0;
    std::wstring note;
};

struct TCfg {
    int  codec   = 7;    // 目标编码
    int  level   = 40;   // 兼容等级（0=自动）
    bool baseline = false;  // 强制 baseline（不要 B 帧/8x8）
};

void  TranscodeInit();
void  TranscodeSetCfg(int codec, int level, bool baseline);
void  TranscodeGetCfg(int* codec, int* level, bool* baseline);
void  TranscodeEnqueue(const std::vector<std::wstring>& files);
int   TranscodeCount();
bool  TranscodeGet(int i, TJob* out);
bool  TranscodeActive();
void  TranscodeClearDone();
std::wstring TranscodeLastOutDir();
bool  TranscodeToolsPresent();
std::wstring TranscodeFfmpegPath();
