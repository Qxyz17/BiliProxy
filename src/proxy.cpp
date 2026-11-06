#include "common.h"
#include "http.h"
#include "playurl.h"
#include "config.h"
#include "proxy.h"
#include "fatality_html.h"
#include "download.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <zlib.h>
#include <brotli/decode.h>

static SOCKET connectTcp(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    char pbuf[16];
    snprintf(pbuf, sizeof pbuf, "%d", port);
    if (getaddrinfo(host.c_str(), pbuf, &hints, &res) != 0) return INVALID_SOCKET;
    SOCKET s = INVALID_SOCKET;
    for (addrinfo* a = res; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (s != INVALID_SOCKET) {
        DWORD t = 30000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof t);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof t);
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
    }
    return s;
}

static bool sendAll(SOCKET s, const char* p, size_t n) {
    while (n) {
        int r = send(s, p, (int)std::min<size_t>(n, 1 << 20), 0);
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    return true;
}

struct Conn {
    SSL* ssl = nullptr;
    std::string pend;
    bool fill() {
        char b[16384];
        int r = SSL_read(ssl, b, sizeof b);
        if (r <= 0) return false;
        pend.append(b, (size_t)r);
        return true;
    }
    bool untilHead(std::string& out) {
        size_t p;
        while ((p = pend.find("\r\n\r\n")) == std::string::npos) {
            if (pend.size() > (1u << 20)) return false;
            if (!fill()) return false;
        }
        out = pend.substr(0, p + 4);
        pend.erase(0, p + 4);
        return true;
    }
    bool untilLine(std::string& out) {
        size_t p;
        while ((p = pend.find("\r\n")) == std::string::npos) {
            if (pend.size() > (1u << 20)) return false;
            if (!fill()) return false;
        }
        out = pend.substr(0, p + 2);
        pend.erase(0, p + 2);
        return true;
    }
    bool readN(size_t n, std::string& out) {
        while (pend.size() < n) if (!fill()) return false;
        out = pend.substr(0, n);
        pend.erase(0, n);
        return true;
    }
    bool readAll(std::string& out) {
        out.swap(pend);
        char b[16384];
        int r;
        while ((r = SSL_read(ssl, b, sizeof b)) > 0) out.append(b, (size_t)r);
        return true;
    }
};

static bool sslWriteAll(SSL* s, const std::string& d) {
    size_t off = 0;
    while (off < d.size()) {
        int n = SSL_write(s, d.data() + off, (int)std::min<size_t>(d.size() - off, 1 << 20));
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

// 向源站发请求，读完整响应；返回 false 表示失败
static bool upstreamRequest(const std::string& host, int port,
                            const std::string& method, const std::string& target,
                            const std::string& version,
                            const std::vector<Header>& reqHeaders,
                            const std::string& reqBody,
                            std::string& statusLine,
                            std::vector<Header>& respHeaders,
                            std::string& respBody) {
    SOCKET fd = connectTcp(host, port);
    if (fd == INVALID_SOCKET) { Log("upstream connect failed %s:%d", host.c_str(), port); return false; }

    SSL* up = SSL_new(g_cliCtx);
    SSL_set_tlsext_host_name(up, host.c_str());
    SSL_set_fd(up, (int)fd);
    static const unsigned char alpn[] = { 8, 'h','t','t','p','/','1','.','1' };
    SSL_set_alpn_protos(up, alpn, sizeof alpn);
    if (SSL_connect(up) != 1) {
        LogSslErr("upstream SSL_connect");
        SSL_free(up);
        closesocket(fd);
        return false;
    }

    std::string req = method + " " + target + " " + version + "\r\n";
    for (auto& h : reqHeaders) {
        std::string ln = lower(h.name);
        if (ln == "proxy-connection" || ln == "connection" || ln == "keep-alive" ||
            ln == "te" || ln == "trailer" || ln == "transfer-encoding" ||
            ln == "upgrade" || ln == "accept-encoding" || ln == "proxy-authorization")
            continue;
        req += h.name + ": " + h.value + "\r\n";
    }
    req += "Accept-Encoding: identity\r\n";
    req += "Connection: close\r\n\r\n";
    req += reqBody;

    bool ok = false;
    Conn c{ up, {} };
    if (sslWriteAll(up, req)) {
        std::string head;
        if (c.untilHead(head)) {
            std::string first;
            if (parseHead(head, first, respHeaders)) {
                statusLine = first;
                std::string te = lower(getHeader(respHeaders, "transfer-encoding"));
                std::string cl = getHeader(respHeaders, "content-length");
                if (te.find("chunked") != std::string::npos) {
                    // 逐块解码
                    std::string body;
                    for (;;) {
                        std::string line;
                        if (!c.untilLine(line)) break;
                        std::string szs = line.substr(0, line.find("\r\n"));
                        size_t semi = szs.find(';');
                        if (semi != std::string::npos) szs = szs.substr(0, semi);
                        unsigned long sz = strtoul(trim(szs).c_str(), nullptr, 16);
                        if (sz == 0) {
                            // 尾部
                            std::string dummy;
                            c.untilLine(dummy);
                            break;
                        }
                        std::string chunk;
                        if (!c.readN(sz, chunk)) break;
                        body += chunk;
                        std::string crlf;
                        c.readN(2, crlf);
                    }
                    respBody.swap(body);
                } else if (!cl.empty()) {
                    size_t n = (size_t)_strtoui64(cl.c_str(), nullptr, 10);
                    if (!c.readN(n, respBody)) { /* 部分也接受 */ }
                } else {
                    c.readAll(respBody);
                }
                ok = true;
            }
        }
    }
    SSL_shutdown(up);
    SSL_free(up);
    closesocket(fd);
    return ok;
}

static std::string buildResponse(const std::string& statusLine, std::vector<Header>& hs,
                                 const std::string& body, bool keepAlive, bool dropEncoding) {
    std::string st = statusLine;
    if (st.rfind("HTTP/", 0) == 0) { size_t sp = st.find(' '); if (sp != std::string::npos) st = st.substr(sp + 1); }
    std::string out = "HTTP/1.1 " + st + "\r\n";
    for (auto& h : hs) {
        std::string ln = lower(h.name);
        if (ln == "connection" || ln == "content-length" || ln == "transfer-encoding" ||
            (dropEncoding && ln == "content-encoding") || ln == "keep-alive" || ln == "proxy-connection")
            continue;
        out += h.name + ": " + h.value + "\r\n";
    }
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    out += "\r\n";
    out += body;
    return out;
}


static std::string jsonEsc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; sprintf(b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

static std::string urlDecode(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '%' && i + 2 < v.size()) {
            auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0'
                                 : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - 'A' + 10); };
            out += (char)(hex(v[i + 1]) * 16 + hex(v[i + 2]));
            i += 2;
        } else if (v[i] == '+') out += ' ';
        else out += v[i];
    }
    return out;
}

static std::string queryParam(const std::string& q, const char* k) {
    std::string key = std::string(k) + "=";
    size_t p = q.find(key);
    if (p == std::string::npos) return {};
    size_t e = q.find('&', p);
    return urlDecode(q.substr(p + key.size(), e == std::string::npos ? std::string::npos : e - p - key.size()));
}

// 页面注入脚本没有扩展后台可用，请求从代理走，绕开页面的 CSP / CORS
static bool fetchUrl(const std::string& url, const std::string& method,
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     const std::string& reqBody, int& status, std::string& respBody) {
    std::string scheme, rest;
    if (url.rfind("https://", 0) == 0) { scheme = "https"; rest = url.substr(8); }
    else if (url.rfind("http://", 0) == 0) { scheme = "http"; rest = url.substr(7); }
    else return false;

    std::string hostPort = rest.substr(0, rest.find('/'));
    std::string target = rest.size() > hostPort.size() ? rest.substr(hostPort.size()) : "/";
    std::string host = hostPort;
    int port = scheme == "https" ? 443 : 80;
    size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        host = hostPort.substr(0, colon);
        port = atoi(hostPort.substr(colon + 1).c_str());
    }
    if (host.empty()) return false;

    SOCKET fd = connectTcp(host, port);
    if (fd == INVALID_SOCKET) { Log("fetch connect failed %s:%d", host.c_str(), port); return false; }

    SSL* up = nullptr;
    if (scheme == "https") {
        up = SSL_new(g_cliCtx);
        SSL_set_tlsext_host_name(up, host.c_str());
        SSL_set_fd(up, (int)fd);
        static const unsigned char alpn[] = { 8, 'h','t','t','p','/','1','.','1' };
        SSL_set_alpn_protos(up, alpn, sizeof alpn);
        if (SSL_connect(up) != 1) {
            LogSslErr("fetch SSL_connect");
            SSL_free(up);
            closesocket(fd);
            return false;
        }
    }

    std::string req = method + " " + target + " HTTP/1.1\r\n"
                      "Host: " + hostPort + "\r\n";
    bool hasLen = false;
    for (auto& h : headers) {
        req += h.first + ": " + h.second + "\r\n";
        if (lower(h.first) == "content-length") hasLen = true;
    }
    // 不带长度时有的网关把 body 当成空的，回 400 invalid arguments
    if (!reqBody.empty() && !hasLen)
        req += "Content-Length: " + std::to_string(reqBody.size()) + "\r\n";
    req += "Accept-Encoding: identity\r\nConnection: close\r\n\r\n";
    req += reqBody;

    auto writeAll = [&](const std::string& d) -> bool {
        if (up) return sslWriteAll(up, d);
        return sendAll(fd, d.data(), d.size());
    };
    auto readSome = [&](char* b, int n) -> int {
        return up ? SSL_read(up, b, n) : recv(fd, b, n, 0);
    };

    bool ok = false;
    if (writeAll(req)) {
        std::string raw;
        char buf[16384];
        int r;
        while ((r = readSome(buf, sizeof buf)) > 0) raw.append(buf, (size_t)r);
        size_t hp = raw.find("\r\n\r\n");
        if (hp != std::string::npos) {
            std::string head = raw.substr(0, hp + 4);
            std::string body = raw.substr(hp + 4);
            std::string first;
            std::vector<Header> rh;
            if (parseHead(head, first, rh)) {
                size_t sp = first.find(' ');
                status = sp == std::string::npos ? 0 : atoi(first.substr(sp + 1).c_str());
                if (lower(getHeader(rh, "transfer-encoding")).find("chunked") != std::string::npos) {
                    std::string dec;
                    size_t i = 0;
                    for (;;) {
                        size_t le = body.find("\r\n", i);
                        if (le == std::string::npos) break;
                        size_t semi = body.find(';', i);
                        if (semi == std::string::npos || semi > le) semi = le;
                        unsigned long sz = strtoul(body.substr(i, semi - i).c_str(), nullptr, 16);
                        if (sz == 0) break;
                        if (le + 2 + sz > body.size()) break;
                        dec.append(body, le + 2, sz);
                        i = le + 2 + sz + 2;
                    }
                    body.swap(dec);
                }
                respBody.swap(body);
                ok = true;
            }
        }
    }

    if (up) { SSL_shutdown(up); SSL_free(up); }
    closesocket(fd);
    return ok;
}

static void serveTls(SSL* ssl, const std::string& host) {
    Conn c{ ssl, {} };
    for (;;) {
        std::string head;
        if (!c.untilHead(head)) break;
        std::string first;
        std::vector<Header> hs;
        if (!parseHead(head, first, hs)) break;

        std::istringstream ls(first);
        std::string method, target, version;
        ls >> method >> target >> version;
        if (method.empty()) break;

        if (target.rfind("/__biliproxy/fetch", 0) == 0) {
            std::string q = target.find('?') == std::string::npos ? "" : target.substr(target.find('?') + 1);
            std::string url = queryParam(q, "url");
            int st = 0;
            std::string rb;
            std::vector<std::pair<std::string, std::string>> hd{{"Referer", "https://www.bilibili.com/"}};
            std::string jr;
            if (url.empty() || !fetchUrl(url, "GET", hd, "", st, rb))
                jr = "{\"ok\":false,\"error\":\"fetch failed\"}";
            else
                jr = "{\"ok\":" + std::string(st >= 200 && st < 400 ? "true" : "false") +
                     ",\"status\":" + std::to_string(st) + ",\"text\":\"" + jsonEsc(rb) + "\"}";
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
                               "Content-Length: " + std::to_string(jr.size()) +
                               "\r\nCache-Control: no-store\r\nConnection: keep-alive\r\n\r\n" + jr;
            if (!sslWriteAll(ssl, resp)) break;
            continue;
        }

        if (target.rfind("/__biliproxy/frames", 0) == 0) {
            std::string q = target.find('?') == std::string::npos ? "" : target.substr(target.find('?') + 1);
            std::vector<std::string> fr;
            std::wstring err;
            bool ok = DlFrames(queryParam(q, "cid"), atoi(queryParam(q, "n").c_str()),
                               atoi(queryParam(q, "w").c_str()), fr, err);
            std::string jr;
            if (!ok) jr = "{\"ok\":false,\"msg\":\"" + jsonEsc(U8(err)) + "\"}";
            else {
                jr = "{\"ok\":true,\"frames\":[";
                for (size_t i = 0; i < fr.size(); ++i) {
                    if (i) jr += ",";
                    jr += "\"data:image/jpeg;base64," + fr[i] + "\"";
                }
                jr += "]}";
            }
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
                               "Content-Length: " + std::to_string(jr.size()) +
                               "\r\nCache-Control: no-store\r\nConnection: keep-alive\r\n\r\n" + jr;
            if (!sslWriteAll(ssl, resp)) break;
            continue;
        }

        if (target.rfind("/__biliproxy/dl", 0) == 0) {
            std::string q = target.find('?') == std::string::npos ? "" : target.substr(target.find('?') + 1);
            auto param = [&](const char* k) { return queryParam(q, k); };
            std::string jr;
            if (target.rfind("/__biliproxy/dlstatus", 0) == 0) {
                int id = atoi(param("id").c_str());
                DlJob j;
                if (!DlGet(id, &j)) jr = "{\"ok\":false}";
                else jr = "{\"ok\":true,\"state\":" + std::to_string(j.state) +
                          ",\"pct\":" + std::to_string(j.percent) +
                          ",\"out\":\"" + jsonEsc(U8(j.out)) + "\",\"note\":\"" + jsonEsc(U8(j.note)) + "\"}";
            } else {
                std::wstring err;
                int id = DlStart(atoi(param("wm").c_str()) ? 1 : 0, param("name"), param("aid"), param("cid"), err);
                jr = id < 0 ? "{\"ok\":false,\"msg\":\"" + jsonEsc(U8(err)) + "\"}"
                            : "{\"ok\":true,\"id\":" + std::to_string(id) + "}";
            }
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
                               "Content-Length: " + std::to_string(jr.size()) +
                               "\r\nCache-Control: no-store\r\nConnection: keep-alive\r\n\r\n" + jr;
            if (!sslWriteAll(ssl, resp)) break;
            continue;
        }

        if (target.rfind("/__biliproxy/fatality.html", 0) == 0) {
            std::string page = kFatalityHtml;
            size_t hp = page.find("</head>");
            if (hp != std::string::npos)
                page.insert(hp, "<style>html,body{margin:0;padding:0;border:0;overflow:hidden;width:100%;height:100%;background:transparent!important}#stage{position:fixed;inset:0;margin:0;padding:0}canvas{display:block;margin:0;padding:0}</style>");
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                               std::to_string(page.size()) + "\r\nCache-Control: no-store\r\nConnection: keep-alive\r\n\r\n" + page;
            if (!sslWriteAll(ssl, resp)) break;
            continue;
        }

        std::string body;
        std::string te = lower(getHeader(hs, "transfer-encoding"));
        std::string cl = getHeader(hs, "content-length");
        if (te.find("chunked") != std::string::npos) {
            for (;;) {
                std::string line;
                if (!c.untilLine(line)) break;
                std::string szs = line.substr(0, line.find("\r\n"));
                size_t semi = szs.find(';');
                if (semi != std::string::npos) szs = szs.substr(0, semi);
                unsigned long sz = strtoul(trim(szs).c_str(), nullptr, 16);
                if (sz == 0) { std::string d; c.untilLine(d); break; }
                std::string chunk;
                if (!c.readN(sz, chunk)) break;
                body += chunk;
                std::string crlf;
                c.readN(2, crlf);
            }
        } else if (!cl.empty()) {
            c.readN((size_t)_strtoui64(cl.c_str(), nullptr, 10), body);
        }

        if (target.rfind("/__biliproxy/ai", 0) == 0) {
            auto jfield = [&](const char* k) -> std::string {
                std::string key = std::string("\"") + k + "\":";
                size_t p = body.find(key);
                if (p == std::string::npos) return {};
                p += key.size();
                while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
                if (p >= body.size() || body[p] != '"') return {};
                ++p;
                std::string out;
                for (size_t i = p; i < body.size(); ++i) {
                    if (body[i] == '\\' && i + 1 < body.size()) {
                        char e = body[++i];
                        if (e == 'n') out += '\n';
                        else if (e == 'r') out += '\r';
                        else if (e == 't') out += '\t';
                        else if (e == 'u' && i + 4 < body.size()) {
                            unsigned cp = (unsigned)strtoul(body.substr(i + 1, 4).c_str(), nullptr, 16);
                            i += 4;
                            if (cp < 0x80) out += (char)cp;
                            else if (cp < 0x800) {
                                out += (char)(0xC0 | (cp >> 6));
                                out += (char)(0x80 | (cp & 0x3F));
                            } else {
                                out += (char)(0xE0 | (cp >> 12));
                                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                                out += (char)(0x80 | (cp & 0x3F));
                            }
                        } else out += e;
                    } else if (body[i] == '"') break;
                    else out += body[i];
                }
                return out;
            };

            std::string url = jfield("url");
            std::string reqBody = jfield("body");
            std::vector<std::pair<std::string, std::string>> hd;
            size_t hk = body.find("\"headers\":{");
            if (hk != std::string::npos) {
                size_t i = hk + 11;
                while (i < body.size()) {
                    while (i < body.size() && (body[i] == ' ' || body[i] == ',' || body[i] == '\n' || body[i] == '\r')) ++i;
                    if (i >= body.size() || body[i] == '}') break;
                    if (body[i] != '"') { ++i; continue; }
                    size_t ne = body.find('"', i + 1);
                    if (ne == std::string::npos) break;
                    std::string name = body.substr(i + 1, ne - i - 1);
                    size_t colon = body.find(':', ne);
                    if (colon == std::string::npos) break;
                    size_t vq = body.find('"', colon);
                    if (vq == std::string::npos) break;
                    std::string val;
                    size_t j = vq + 1;
                    for (; j < body.size(); ++j) {
                        if (body[j] == '\\' && j + 1 < body.size()) { val += body[j]; val += body[++j]; }
                        else if (body[j] == '"') break;
                        else val += body[j];
                    }
                    hd.push_back({ name, val });
                    i = j + 1;
                }
            }

            int st = 0;
            std::string rb;
            std::string jr;
            if (url.empty() || !fetchUrl(url, "POST", hd, reqBody, st, rb))
                jr = "{\"ok\":false,\"error\":\"fetch failed\"}";
            else
                jr = "{\"ok\":" + std::string(st >= 200 && st < 400 ? "true" : "false") +
                     ",\"status\":" + std::to_string(st) + ",\"text\":\"" + jsonEsc(rb) + "\"}";
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
                               "Content-Length: " + std::to_string(jr.size()) +
                               "\r\nCache-Control: no-store\r\nConnection: keep-alive\r\n\r\n" + jr;
            if (!sslWriteAll(ssl, resp)) break;
            continue;
        }

        std::string statusLine;
        std::vector<Header> rh;
        std::string rbody;
        // 目标不是 AV1 时，先让服务端别下发 AV1-only 的流
        std::string upTarget = target;
        if (g_cfg.codec != 0 && target.find("/playurl") != std::string::npos) {
            std::string t2 = stripAv1Bit(target);
            if (t2 != target) {
                upTarget = t2;
                Log("strip fnval av1 bit: %s", t2.c_str());
            }
        }
        // www 页面注入依赖 200 全量响应体，条件请求回 304 时浏览器会拿旧缓存，剥掉让它每次都回 200
        if (host == "www.bilibili.com") {
            for (auto it = hs.begin(); it != hs.end();) {
                std::string n = lower(it->name);
                if (n == "if-none-match" || n == "if-modified-since") it = hs.erase(it);
                else ++it;
            }
        }
        if (!upstreamRequest(host, 443, method, upTarget, version, hs, body, statusLine, rh, rbody)) {
            std::string err = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            sslWriteAll(ssl, err);
            break;
        }

        if (g_verbose) Log("up %s %s -> [%s] %zu bytes", host.c_str(), target.c_str(), statusLine.c_str(), rbody.size());
        bool decoded = false;
        std::string enc = lower(getHeader(rh, "content-encoding"));
        if (!enc.empty() && !rbody.empty()) {
            std::string dec;
            bool ok = false;
            if (enc.find("br") != std::string::npos) ok = unbrotli(rbody, dec);
            else if (enc.find("gzip") != std::string::npos) ok = gunzip(rbody, dec);
            if (ok) { rbody.swap(dec); decoded = true; }
        }
        bool rewritten = false;
        if ((enc.empty() || decoded) && target.find("/playurl") != std::string::npos &&
            rbody.find("\"video\"") != std::string::npos) {
            std::string q = target.find('?') == std::string::npos ? "" : target.substr(target.find('?') + 1);
            DlCachePlayurl(rbody, q);
        }
        if (g_cfg.codec != 0 && (enc.empty() || decoded) && target.find("/playurl") != std::string::npos &&
            rbody.find("\"video\"") != std::string::npos) {
            std::string f = filterPlayurlJson(rbody, g_cfg.codec);
            if (!f.empty()) {
                if (f != rbody) { rbody.swap(f); rewritten = true; }
            } else {
                // 该清晰度只有 AV1：尝试按更低清晰度重新取源
                static const int kQn[] = { 80, 74, 64, 32 };
                for (int q : kQn) {
                    std::string t2 = setQn(upTarget, q);
                    if (t2 == target) break;
                    std::string sl2; std::vector<Header> rh2; std::string rb2;
                    if (!upstreamRequest(host, 443, method, t2, version, hs, body, sl2, rh2, rb2)) continue;
                    std::string e2 = lower(getHeader(rh2, "content-encoding"));
                    if (!e2.empty()) {
                        std::string dec;
                        bool ok = e2.find("br") != std::string::npos ? unbrotli(rb2, dec) : gunzip(rb2, dec);
                        if (!ok) continue;
                        rb2.swap(dec);
                    }
                    std::string f2 = filterPlayurlJson(rb2, g_cfg.codec);
                    if (!f2.empty()) {
                        rbody.swap(f2); statusLine = sl2; rh = rh2; decoded = true; rewritten = true;
                        Log("fallback qn=%d", q);
                        break;
                    }
                }
            }
        }
        if (!rewritten && g_cfg.codec != 0 && target.find("/playurl") != std::string::npos &&
            rbody.find("\"video\"") != std::string::npos)
            Log("no codec %d available (passthrough) %s", g_cfg.codec, target.c_str());
        if (rewritten) Log("rewrite %s %s", host.c_str(), target.c_str());

        bool injected = false;
        if ((enc.empty() || decoded) && host == "www.bilibili.com" &&
            lower(getHeader(rh, "content-type")).find("text/html") != std::string::npos &&
            rbody.find("</head>") != std::string::npos) {
            if (g_cfg.blockLogin) rbody = injectNoLogin(rbody);
            rbody = injectWebUi(rbody);
            rbody = injectFatality(rbody);
            injected = true;
        }
        if (rewritten) InterlockedIncrement(&g_rewrites);

        std::string resp = buildResponse(statusLine, rh, rbody, true, decoded || rewritten || injected);
        if (!sslWriteAll(ssl, resp)) break;
    }
}

static void tunnel(SOCKET a, SOCKET b) {
    char buf[32768];
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(a, &rd);
        FD_SET(b, &rd);
        timeval tv{ 60, 0 };
        int n = select(0, &rd, nullptr, nullptr, &tv);
        if (n <= 0) break;
        if (FD_ISSET(a, &rd)) {
            int r = recv(a, buf, sizeof buf, 0);
            if (r <= 0) break;
            if (!sendAll(b, buf, (size_t)r)) break;
        }
        if (FD_ISSET(b, &rd)) {
            int r = recv(b, buf, sizeof buf, 0);
            if (r <= 0) break;
            if (!sendAll(a, buf, (size_t)r)) break;
        }
    }
}

static bool isInterceptHost(const std::string& h) {
    std::string x = lower(h);
    if (x == "api.bilibili.com" || x == "api.bilibili.tv") return true;
    if (x == "www.bilibili.com") return true;
    return false;
}

static void handleClient(SOCKET c) {
    // 读取明文请求头（CONNECT 或 PAC）
    std::string raw;
    char buf[4096];
    while (raw.find("\r\n\r\n") == std::string::npos) {
        int r = recv(c, buf, sizeof buf, 0);
        if (r <= 0) { closesocket(c); return; }
        raw.append(buf, (size_t)r);
        if (raw.size() > 65536) { closesocket(c); return; }
    }
    std::string head = raw.substr(0, raw.find("\r\n\r\n") + 4);
    std::string first;
    std::vector<Header> hs;
    parseHead(head, first, hs);
    std::istringstream ls(first);
    std::string method, target, version;
    ls >> method >> target >> version;

    if (method == "CONNECT") {
        std::string host = target;
        int port = 443;
        size_t colon = target.rfind(':');
        if (colon != std::string::npos) {
            host = target.substr(0, colon);
            port = atoi(target.substr(colon + 1).c_str());
        }
        if (!isInterceptHost(host)) {
            SOCKET up = connectTcp(host, port);
            if (up == INVALID_SOCKET) { closesocket(c); return; }
            sendAll(c, "HTTP/1.1 200 Connection Established\r\n\r\n", 39);
            tunnel(c, up);
            closesocket(up);
            closesocket(c);
            return;
        }
        sendAll(c, "HTTP/1.1 200 Connection Established\r\n\r\n", 39);
        SSL* ssl = SSL_new(g_srvCtx);
        SSL_set_fd(ssl, (int)c);
        if (SSL_accept(ssl) == 1) {
            serveTls(ssl, host);
            SSL_shutdown(ssl);
        } else {
            LogSslErr("SSL_accept");
        }
        SSL_free(ssl);
        closesocket(c);
        return;
    }

    // 直接 GET /proxy.pac（WinINET 拉取 PAC）
    if (target.find("/proxy.pac") != std::string::npos) {
        std::string body = pacScript();
        std::string r = "HTTP/1.1 200 OK\r\nContent-Type: application/x-ns-proxy-autoconfig\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        sendAll(c, r.data(), r.size());
        closesocket(c);
        return;
    }

    // 其他一律拒绝
    const char* nf = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    sendAll(c, nf, strlen(nf));
    closesocket(c);
}

static DWORD WINAPI acceptThread(LPVOID) {
    for (;;) {
        SOCKET c = accept(g_listenSock, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        std::thread(handleClient, c).detach();
    }
    return 0;
}

bool startProxy() {
    g_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSock == INVALID_SOCKET) return false;
    BOOL yes = TRUE;
    setsockopt(g_listenSock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&yes, sizeof yes);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)kPort);
    a.sin_addr.s_addr = inet_addr(kListen);
    if (bind(g_listenSock, (sockaddr*)&a, sizeof a) != 0) {
        Log("bind failed %d", WSAGetLastError());
        return false;
    }
    if (listen(g_listenSock, 64) != 0) return false;
    CreateThread(nullptr, 0, acceptThread, nullptr, 0, nullptr);
    Log("proxy listening on %s:%d", kListen, kPort);
    return true;
}



// ------------------------------------------------------------------ GUI

#include <gdiplus.h>

using namespace Gdiplus;

enum {
    IDC_CARD_AUTO = 1, IDC_CARD_AVC, IDC_CARD_HEVC, IDC_CARD_AV1,
    IDC_TOGGLE_NOLOGIN, IDC_TOGGLE_AUTOSTART,
    IDC_BTN_LOG, IDC_BTN_QUIT, IDC_BTN_CLOSE
};
