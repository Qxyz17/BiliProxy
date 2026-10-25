//直连api.bilibili.com测试TLS，打印每步结果
#include <winsock2.h>
#include <ws2tcpip.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <cstdio>
#include <string>

int main() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    addrinfo* ai = nullptr;
    addrinfo hint{};
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    int r = getaddrinfo("api.bilibili.com", "443", &hint, &ai);
    printf("getaddrinfo=%d ai=%p\n", r, (void*)ai);
    if (r != 0 || !ai) return 1;
    char ip[64] = {};
    inet_ntop(AF_INET, &((sockaddr_in*)ai->ai_addr)->sin_addr, ip, sizeof ip);
    printf("ip=%s\n", ip);

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    int c = connect(s, ai->ai_addr, (int)ai->ai_addrlen);
    printf("connect=%d wserr=%d\n", c, WSAGetLastError());
    freeaddrinfo(ai);
    if (c != 0) { closesocket(s); return 1; }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    printf("ctx=%p\n", (void*)ctx);
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, (int)s);
    SSL_set_tlsext_host_name(ssl, "api.bilibili.com");
    int sc = SSL_connect(ssl);
    printf("SSL_connect=%d\n", sc);
    if (sc != 1) {
        ERR_print_errors_fp(stdout);
        closesocket(s);
        return 1;
    }
    printf("cipher=%s ver=%s\n", SSL_get_cipher_name(ssl), SSL_get_version(ssl));

    std::string req =
        "GET /x/web-interface/nav HTTP/1.1\r\n"
        "Host: api.bilibili.com\r\n"
        "User-Agent: Mozilla/5.0\r\n"
        "Connection: close\r\n\r\n";
    SSL_write(ssl, req.data(), (int)req.size());
    char buf[4096];
    std::string out;
    for (;;) {
        int n = SSL_read(ssl, buf, sizeof buf);
        if (n <= 0) break;
        out.append(buf, n);
    }
    printf("resp %zu bytes:\n%.300s\n", out.size(), out.c_str());
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    closesocket(s);
    return 0;
}
