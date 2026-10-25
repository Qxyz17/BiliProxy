// 直接对真实的 B 站 playurl 响应做过滤，验证：
//  1) 严格模式只保留目标编码
//  2) AV1-only 的清晰度会返回空（触发降清晰度兜底）
//  3) fnval 去 AV1 位
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

#include "../src/common.h"
#include "../src/playurl.h"
#include "../src/http.h"

static std::string sslReadAll(SSL* ssl) {
    std::string buf;
    char tmp[16384];
    for (;;) {
        int n = SSL_read(ssl, tmp, sizeof tmp);
        if (n <= 0) break;
        buf.append(tmp, n);
        if (buf.size() > 8u << 20) break;
    }
    return buf;
}

static std::string httpGet(const std::string& host, const std::string& path,
                           const std::string& cookie) {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_default_verify_paths(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(443);
    addrinfo* ai = nullptr;
    addrinfo hint{};
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), "443", &hint, &ai) != 0 || !ai) { closesocket(s); return {}; }
    a.sin_addr = ((sockaddr_in*)ai->ai_addr)->sin_addr;
    freeaddrinfo(ai);
    if (connect(s, (sockaddr*)&a, sizeof a) != 0) { closesocket(s); return {}; }

    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, s);
    SSL_set_tlsext_host_name(ssl, host.c_str());
    if (SSL_connect(ssl) != 1) { SSL_free(ssl); closesocket(s); return {}; }

    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host +
                      "\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
                      "Accept: */*\r\nReferer: https://www.bilibili.com/\r\n";
    if (!cookie.empty()) req += "Cookie: " + cookie + "\r\n";
    req += "Connection: close\r\n\r\n";
    SSL_write(ssl, req.data(), (int)req.size());

    std::string raw = sslReadAll(ssl);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    closesocket(s);
    SSL_CTX_free(ctx);

    size_t hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return {};
    std::string head = raw.substr(0, hdrEnd);
    std::string body = raw.substr(hdrEnd + 4);

    // 简单解压
    std::string enc;
    std::string lhead = lower(head);
    size_t ce = lhead.find("content-encoding:");
    if (ce != std::string::npos) {
        size_t vs = head.find_first_not_of(" \t", ce + 17);
        size_t ve = head.find("\r\n", vs);
        enc = lower(head.substr(vs, ve - vs));
    }
    if (enc.find("gzip") != std::string::npos) {
        std::string dec;
        if (gunzip(body, dec)) body = dec;
    } else if (enc.find("br") != std::string::npos) {
        std::string dec;
        if (unbrotli(body, dec)) body = dec;
    }
    return body;
}

static void dumpCodecs(const std::string& body) {
    size_t key = body.find("\"video\"");
    if (key == std::string::npos) { printf("  no \"video\" key\n"); return; }
    size_t i = body.find("\"codecid\"", key);
    int n = 0;
    while (i != std::string::npos && n < 40) {
        size_t c = body.find(':', i + 8);
        int v = atoi(body.c_str() + c + 1);
        // 找同一段里的 codecs
        size_t cs = body.find("\"codecs\"", i);
        std::string codecs = "?";
        if (cs != std::string::npos && cs - i < 2000) {
            size_t q1 = body.find('"', body.find(':', cs) + 1);
            size_t q2 = body.find('"', q1 + 1);
            codecs = body.substr(q1 + 1, q2 - q1 - 1);
        }
        // 找 id
        size_t idp = body.rfind("\"id\"", i);
        int id = 0;
        if (idp != std::string::npos && i - idp < 2000) id = atoi(body.c_str() + body.find(':', idp) + 1);
        printf("  codecid=%-3d codecs=%-8s id=%d\n", v, codecs.c_str(), id);
        ++n;
        i = body.find("\"codecid\"", i + 8);
    }
}

int main(int argc, char** argv) {
    std::string avid = argc > 1 ? argv[1] : "";
    std::string cookie = argc > 2 ? argv[2] : "";

    std::string path = "/x/player/wbi/playurl?avid=" + avid +
                       "&fnval=4048&fnver=0&fourk=1&platform=pc&qn=80";

    printf("=== 原始响应 (fnval=4048) ===\n");
    std::string body = httpGet("api.bilibili.com", path, cookie);
    if (body.empty()) { printf("  请求失败\n"); return 1; }
    printf("  body %zu bytes: %.200s\n", body.size(), body.c_str());
    dumpCodecs(body);

    printf("\n=== 过滤后 (want=7 H.264) ===\n");
    std::string f = filterPlayurlJson(body, 7);
    if (f.empty()) printf("  空 -> 该清晰度没有 H.264\n");
    else dumpCodecs(f);

    printf("\n=== fnval 去 AV1 位 ===\n");
    std::string t2 = stripAv1Bit(path);
    printf("  %s\n", t2.c_str());

    printf("\n=== 用去 AV1 的 fnval 重新请求 ===\n");
    std::string body2 = httpGet("api.bilibili.com", t2, cookie);
    if (body2.empty()) printf("  请求失败\n");
    else {
        dumpCodecs(body2);
        printf("\n=== 过滤后 (want=7) ===\n");
        std::string f2 = filterPlayurlJson(body2, 7);
        if (f2.empty()) printf("  空\n");
        else dumpCodecs(f2);
    }
    return 0;
}
