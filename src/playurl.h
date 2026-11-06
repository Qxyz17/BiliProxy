#pragma once

#include "common.h"

std::string filterPlayurlJson(const std::string& body, int want);
std::string setQn(const std::string& target, int qn);
// 从 fnval 里去掉 AV1 位（2048），服务端就不会下发只有 AV1 的流
std::string stripAv1Bit(const std::string& target);
std::string injectNoLogin(const std::string& html);
std::string injectFatality(const std::string& html);
std::string injectWebUi(const std::string& html);

