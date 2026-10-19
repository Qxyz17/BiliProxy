#pragma once

#include "common.h"

struct Header {
    std::string name, value;
};

bool parseHead(const std::string& head, std::string& first, std::vector<Header>& hs);
std::string getHeader(const std::vector<Header>& hs, const std::string& name);
bool gunzip(const std::string& in, std::string& out);
bool unbrotli(const std::string& in, std::string& out);
