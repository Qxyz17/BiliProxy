#include "common.h"
#include "cert.h"

bool genKeyCert() {
    std::wstring caPem = g_dir + L"\\certs\\ca.pem";
    std::wstring caKey = g_dir + L"\\certs\\ca.key";
    std::wstring lfPem = g_dir + L"\\certs\\leaf.pem";
    std::wstring lfKey = g_dir + L"\\certs\\leaf.key";
    std::wstring caDer = g_dir + L"\\certs\\ca.cer";

    if (fileExists(caPem) && fileExists(caKey) && fileExists(lfPem) && fileExists(lfKey)) {
        Log("certs: reuse existing");
        return true;
    }

    Log("certs: generating new CA + leaf");
    EVP_PKEY* caPkey = nullptr;
    EVP_PKEY* lfPkey = nullptr;
    X509* ca = nullptr;
    X509* leaf = nullptr;
    bool ok = false;

    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!kctx) goto done;
    if (EVP_PKEY_keygen_init(kctx) <= 0) goto done;
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 2048) <= 0) goto done;
    if (EVP_PKEY_keygen(kctx, &caPkey) <= 0) goto done;
    EVP_PKEY_CTX_free(kctx);
    kctx = nullptr;

    ca = X509_new();
    if (!ca) goto done;
    X509_set_version(ca, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(ca), 0x6B696C69);
    X509_gmtime_adj(X509_getm_notBefore(ca), -86400L);
    X509_gmtime_adj(X509_getm_notAfter(ca), 10L * 365 * 24 * 3600);
    X509_set_pubkey(ca, caPkey);
    {
        X509_NAME* n = X509_get_subject_name(ca);
        X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_UTF8, (const unsigned char*)kCaSubjectU8, -1, -1, 0);
        X509_NAME_add_entry_by_txt(n, "O",  MBSTRING_UTF8, (const unsigned char*)"BiliProxy", -1, -1, 0);
        X509_set_issuer_name(ca, n);
    }
    {
        X509V3_CTX x;
        X509V3_set_ctx_nodb(&x);
        X509V3_set_ctx(&x, ca, ca, nullptr, nullptr, 0);
        X509_EXTENSION* e1 = X509V3_EXT_conf_nid(nullptr, &x, NID_basic_constraints, "critical,CA:TRUE");
        X509_EXTENSION* e2 = X509V3_EXT_conf_nid(nullptr, &x, NID_key_usage, "critical,keyCertSign,cRLSign");
        if (e1) { X509_add_ext(ca, e1, -1); X509_EXTENSION_free(e1); }
        if (e2) { X509_add_ext(ca, e2, -1); X509_EXTENSION_free(e2); }
    }
    if (X509_sign(ca, caPkey, EVP_sha256()) <= 0) goto done;

    // leaf
    kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!kctx) goto done;
    if (EVP_PKEY_keygen_init(kctx) <= 0) goto done;
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 2048) <= 0) goto done;
    if (EVP_PKEY_keygen(kctx, &lfPkey) <= 0) goto done;
    EVP_PKEY_CTX_free(kctx);
    kctx = nullptr;

    leaf = X509_new();
    if (!leaf) goto done;
    X509_set_version(leaf, 2);
    {
        unsigned char rnd[8];
        RAND_bytes(rnd, sizeof rnd);
        long v = 1;
        for (unsigned char c : rnd) v = (v * 131 + c) & 0x7FFFFFFFFFFFFFFFL;
        ASN1_INTEGER_set(X509_get_serialNumber(leaf), v);
    }
    X509_gmtime_adj(X509_getm_notBefore(leaf), -86400L);
    X509_gmtime_adj(X509_getm_notAfter(leaf), 5L * 365 * 24 * 3600);
    X509_set_pubkey(leaf, lfPkey);
    {
        X509_NAME* n = X509_NAME_new();
        X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_UTF8, (const unsigned char*)"api.bilibili.com", -1, -1, 0);
        X509_NAME_add_entry_by_txt(n, "O",  MBSTRING_UTF8, (const unsigned char*)"BiliProxy", -1, -1, 0);
        X509_set_subject_name(leaf, n);
        X509_NAME_free(n);
        X509_set_issuer_name(leaf, X509_get_subject_name(ca));
    }
    {
        X509V3_CTX x;
        X509V3_set_ctx_nodb(&x);
        X509V3_set_ctx(&x, ca, leaf, nullptr, nullptr, 0);
        struct { int nid; const char* val; } exts[] = {
            { NID_basic_constraints,      "critical,CA:FALSE" },
            { NID_key_usage,              "critical,digitalSignature,keyEncipherment" },
            { NID_ext_key_usage,          "serverAuth" },
            { NID_subject_alt_name,       "DNS:api.bilibili.com,DNS:www.bilibili.com,DNS:bilibili.com,DNS:*.bilibili.com" },
        };
        for (auto& e : exts) {
            X509_EXTENSION* x5 = X509V3_EXT_conf_nid(nullptr, &x, e.nid, e.val);
            if (x5) { X509_add_ext(leaf, x5, -1); X509_EXTENSION_free(x5); }
        }
    }
    if (X509_sign(leaf, caPkey, EVP_sha256()) <= 0) goto done;

    {
        FILE* f = _wfopen(caPem.c_str(), L"wb");
        if (!f) goto done;
        PEM_write_X509(f, ca);
        fclose(f);
        f = _wfopen(caKey.c_str(), L"wb");
        if (!f) goto done;
        PEM_write_PrivateKey(f, caPkey, nullptr, nullptr, 0, nullptr, nullptr);
        fclose(f);
        f = _wfopen(lfPem.c_str(), L"wb");
        if (!f) goto done;
        PEM_write_X509(f, leaf);
        fclose(f);
        f = _wfopen(lfKey.c_str(), L"wb");
        if (!f) goto done;
        PEM_write_PrivateKey(f, lfPkey, nullptr, nullptr, 0, nullptr, nullptr);
        fclose(f);
        // DER 版，便于 certutil 安装
        std::string der;
        der.resize((size_t)i2d_X509(ca, nullptr));
        { unsigned char* p = (unsigned char*)der.data(); i2d_X509(ca, &p); }
        writeFile(caDer, der);
    }
    ok = true;

done:
    if (kctx) EVP_PKEY_CTX_free(kctx);
    if (ca) X509_free(ca);
    if (leaf) X509_free(leaf);
    if (caPkey) EVP_PKEY_free(caPkey);
    if (lfPkey) EVP_PKEY_free(lfPkey);
    if (!ok) LogSslErr("genKeyCert");
    return ok;
}

bool loadCerts() {
    std::wstring caPem = g_dir + L"\\certs\\ca.pem";
    std::wstring lfPem = g_dir + L"\\certs\\leaf.pem";
    std::wstring lfKey = g_dir + L"\\certs\\leaf.key";

    FILE* f = _wfopen(lfPem.c_str(), L"rb");
    if (!f) return false;
    X509* leaf = PEM_read_X509(f, nullptr, nullptr, nullptr);
    fclose(f);
    f = _wfopen(lfKey.c_str(), L"rb");
    if (!f) { X509_free(leaf); return false; }
    EVP_PKEY* key = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
    fclose(f);

    g_srvCtx = SSL_CTX_new(TLS_server_method());
    g_cliCtx = SSL_CTX_new(TLS_client_method());
    if (!g_srvCtx || !g_cliCtx || !leaf || !key) {
        LogSslErr("loadCerts");
        return false;
    }
    SSL_CTX_set_min_proto_version(g_srvCtx, TLS1_2_VERSION);
    SSL_CTX_set_min_proto_version(g_cliCtx, TLS1_2_VERSION);
    if (SSL_CTX_use_certificate(g_srvCtx, leaf) != 1 ||
        SSL_CTX_use_PrivateKey(g_srvCtx, key) != 1) {
        LogSslErr("use_cert");
        return false;
    }
    SSL_CTX_set_alpn_select_cb(g_srvCtx, [](SSL*, const unsigned char** out, unsigned char* outlen,
                                            const unsigned char* in, unsigned int inlen, void*) -> int {
        static const unsigned char p[] = { 8, 'h','t','t','p','/','1','.','1' };
        if (SSL_select_next_proto((unsigned char**)out, outlen, p, sizeof p, in, inlen) != OPENSSL_NPN_NEGOTIATED)
            return SSL_TLSEXT_ERR_NOACK;
        return SSL_TLSEXT_ERR_OK;
    }, nullptr);
    SSL_CTX_set_verify(g_cliCtx, SSL_VERIFY_NONE, nullptr);
    X509_free(leaf);
    EVP_PKEY_free(key);
    return true;
}

bool rootCaPresent() {
    std::string der = readFile(g_dir + L"\\certs\\ca.cer");
    if (der.empty()) return false;
    HCERTSTORE st = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                  CERT_SYSTEM_STORE_CURRENT_USER, L"Root");
    if (!st) return false;
    bool found = false;
    PCCERT_CONTEXT ctx = nullptr;
    while ((ctx = CertEnumCertificatesInStore(st, ctx)) != nullptr) {
        if (ctx->cbCertEncoded == der.size() &&
            memcmp(ctx->pbCertEncoded, der.data(), der.size()) == 0) {
            found = true;
            break;
        }
    }
    if (ctx) CertFreeCertificateContext(ctx);
    CertCloseStore(st, 0);
    return found;
}

bool installRootCa() {
    std::string der = readFile(g_dir + L"\\certs\\ca.cer");
    if (der.empty()) { Log("installRootCa: no ca.cer"); return false; }
    if (rootCaPresent()) { Log("root CA already trusted"); return true; }
    HCERTSTORE st = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                  CERT_SYSTEM_STORE_CURRENT_USER, L"Root");
    if (!st) { Log("installRootCa: open store %lu", GetLastError()); return false; }
    BOOL ok = CertAddEncodedCertificateToStore(st, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                    (const BYTE*)der.data(), (DWORD)der.size(),
                    CERT_STORE_ADD_REPLACE_EXISTING, nullptr);
    CertCloseStore(st, 0);
    if (!ok) Log("installRootCa: add failed %lu", GetLastError());
    else Log("root CA installed (user store)");
    return ok != FALSE;
}

void deleteCaFromStore(const wchar_t* storeName, const unsigned char* md, unsigned int mdlen) {
    CRYPT_HASH_BLOB blob{};
    blob.cbData = mdlen;
    blob.pbData = const_cast<unsigned char*>(md);
    HCERTSTORE st = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                  CERT_SYSTEM_STORE_CURRENT_USER, storeName);
    if (!st) return;
    PCCERT_CONTEXT ctx = CertFindCertificateInStore(st, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                                    CERT_FIND_SHA1_HASH, &blob, nullptr);
    if (ctx) {
        CertDeleteCertificateFromStore(ctx);
        Log("CA removed from %ls", storeName);
    }
    CertCloseStore(st, 0);
}

void removeRootCa() {
    FILE* f = _wfopen((g_dir + L"\\certs\\ca.pem").c_str(), L"rb");
    if (!f) return;
    X509* ca = PEM_read_X509(f, nullptr, nullptr, nullptr);
    fclose(f);
    if (!ca) return;
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdlen = 0;
    X509_digest(ca, EVP_sha1(), md, &mdlen);
    X509_free(ca);
    deleteCaFromStore(L"Root", md, mdlen);
    deleteCaFromStore(L"TrustedPeople", md, mdlen);
    DeleteFileW((g_dir + L"\\installed.flag").c_str());
}

static std::string pacScript() {
    char buf[1600];
    snprintf(buf, sizeof buf,
        "function FindProxyForURL(url, host) {\n"
        "  if (host == \"api.bilibili.com\" || host == \"api.bilibili.tv\")\n"
        "    return \"PROXY 127.0.0.1:%d; DIRECT\";\n"
        "  if (%d && host == \"www.bilibili.com\")\n"
        "    return \"PROXY 127.0.0.1:%d; DIRECT\";\n"
        "  return \"DIRECT\";\n"
        "}\n", kPort, g_cfg.blockLogin ? 1 : 0, kPort);
    return buf;
}

static void setPac(bool on) {
    INTERNET_PER_CONN_OPTION_LIST list{};
    INTERNET_PER_CONN_OPTION opt[2]{};
    list.dwSize = sizeof list;
    list.pszConnection = nullptr;
    list.dwOptionCount = on ? 2 : 1;
    list.pOptions = opt;
    opt[0].dwOption = INTERNET_PER_CONN_FLAGS;
    opt[0].Value.dwValue = on ? (PROXY_TYPE_AUTO_PROXY_URL | PROXY_TYPE_DIRECT) : PROXY_TYPE_DIRECT;
    static wchar_t url[160];
    if (on) {
        swprintf(url, 160, L"http://127.0.0.1:%d/proxy.pac?v=%lu", kPort, ++g_pacVersion);
        opt[1].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
        opt[1].Value.pszValue = url;
    }
    BOOL ok = InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, list.dwSize);
    Log("setPac(%d) per-connection=%d", on ? 1 : 0, ok ? 1 : 0);
    if (ok) InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    Log("PAC %s", on ? "enabled" : "disabled");
}

static void addAutostart(const std::wstring& exePath) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    std::wstring v = L"\"" + exePath + L"\" --autostart";
    RegSetKeyValueW(k, nullptr, kRunValue, REG_SZ, v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    Log("autostart registered: %ls", exePath.c_str());
}

// 把自己复制到 %LOCALAPPDATA%\BiliProxy，避免用户移动/删除源码目录后自启失效
static std::wstring ensureInstalledCopy() {
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring target = g_dir + L"\\BiliProxy.exe";
    if (_wcsicmp(self, target.c_str()) == 0) return target;
    if (CopyFileW(self, target.c_str(), FALSE)) {
        Log("copied self to %ls", target.c_str());
        return target;
    }
    Log("copy self failed %lu", GetLastError());
    return self;
}

static void removeAutostart() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    RegDeleteKeyValueW(k, nullptr, kRunValue);
    RegCloseKey(k);
}

