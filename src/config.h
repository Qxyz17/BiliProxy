#pragma once

#include "common.h"

std::string pacScript();
void loadConfig();
void saveConfig();
void setPac(bool on);
void addAutostart(const std::wstring& exePath);
void removeAutostart();
void RemoveAllAutostart();
void MigrateOldData();
std::wstring ensureInstalledCopy();

// 本地转码偏好（与网页改写互不影响）
void loadLocalCfg();
void saveLocalCfg(int codec, int level, bool baseline);
