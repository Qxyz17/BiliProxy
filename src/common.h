#pragma once

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <wininet.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <fstream>
#include <sstream>
#include <algorithm>

extern const int         kPort;
extern const char*       kListen;
extern const wchar_t*    kRunValue;
extern const wchar_t*    kCaSubject;
extern const char*       kCaSubjectU8;

struct Settings {
    int  codec      = 7;
    bool blockLogin = true;
    bool autostart  = true;
    bool startupAnim = true;
    bool webAnim    = true;
    bool chatPanel  = true;
    bool downloadBtn = true;
};

extern Settings      g_cfg;
extern std::wstring  g_dir;
extern std::string   g_dirU8;
extern unsigned long g_pacVersion;
extern LONG          g_rewrites;
extern SSL_CTX*      g_srvCtx;
extern SSL_CTX*      g_cliCtx;
extern std::mutex    g_logMu;
extern FILE*         g_logFile;
extern bool          g_verbose;
extern SOCKET        g_listenSock;

std::string U8(const std::wstring& w);
std::wstring W(const std::string& s);
std::string lower(std::string s);
std::string trim(const std::string& s);
bool fileExists(const std::wstring& p);
void Log(const char* fmt, ...);
void LogSslErr(const char* where);
std::string readFile(const std::wstring& p);
bool writeFile(const std::wstring& p, const std::string& d);
