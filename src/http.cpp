#include "common.h"
#include "http.h"

#include <zlib.h>
#include <brotli/decode.h>

bool parseHead(const std::string& head, std::string& first, std::vector<Header>& hs) {
    size_t p = head.find("\r\n");
    if (p == std::string::npos) return false;
    first = head.substr(0, p);
    size_t i = p + 2;
    while (i < head.size()) {
        size_t e = head.find("\r\n", i);
        if (e == std::string::npos || e == i) break;
        std::string line = head.substr(i, e - i);
        size_t c = line.find(':');
        if (c != std::string::npos) {
            Header h;
            h.name = trim(line.substr(0, c));
            h.value = trim(line.substr(c + 1));
            hs.push_back(h);
        }
        i = e + 2;
    }
    return true;
}

std::string getHeader(const std::vector<Header>& hs, const std::string& name) {
    std::string n = lower(name);
    for (auto& h : hs) if (lower(h.name) == n) return h.value;
    return {};
}

// ------------------------------------------------------------------ 解压

bool gunzip(const std::string& in, std::string& out) {
    z_stream zs{};
    if (inflateInit2(&zs, 15 + 32) != Z_OK) return false;
    zs.next_in = (Bytef*)in.data();
    zs.avail_in = (uInt)in.size();
    char buf[65536];
    int ret = Z_OK;
    while (ret == Z_OK || ret == Z_BUF_ERROR) {
        zs.next_out = (Bytef*)buf;
        zs.avail_out = sizeof buf;
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) break;
        out.append(buf, sizeof buf - zs.avail_out);
        if (ret == Z_STREAM_END) break;
        if (zs.avail_in == 0) break;
    }
    inflateEnd(&zs);
    return ret == Z_STREAM_END;
}

bool unbrotli(const std::string& in, std::string& out) {
    size_t cap = in.size() * 4 + 1024;
    for (int tries = 0; tries < 8; ++tries) {
        std::string tmp;
        tmp.resize(cap);
        size_t sz = cap;
        BrotliDecoderResult r = BrotliDecoderDecompress(in.size(), (const uint8_t*)in.data(), &sz, (uint8_t*)tmp.data());
        if (r == BROTLI_DECODER_RESULT_SUCCESS) {
            tmp.resize(sz);
            out.swap(tmp);
            return true;
        }
        if (r != BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) return false;
        cap *= 2;
    }
    return false;
}

// ------------------------------------------------------------------ dash.video 过滤
