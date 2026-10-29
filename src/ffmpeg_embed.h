#pragma once

#include "common.h"

// 从 exe 内嵌数据解出 ffmpeg.exe，返回可用路径（失败返回空串）
std::wstring EnsureEmbeddedFfmpeg();
