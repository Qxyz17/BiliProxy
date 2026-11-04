#include "common.h"
#include "playurl.h"

static int findCodecid(const std::string& obj) {
    size_t p = obj.find("\"codecid\"");
    if (p == std::string::npos) return -1;
    p = obj.find(':', p + 8);
    if (p == std::string::npos) return -1;
    ++p;
    while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) ++p;
    int v = 0;
    bool any = false;
    while (p < obj.size() && obj[p] >= '0' && obj[p] <= '9') { v = v * 10 + (obj[p] - '0'); ++p; any = true; }
    return any ? v : -1;
}

// 返回修改后的 body；未修改则返回空串
std::string filterPlayurlJson(const std::string& body, int want) {
    size_t key = body.find("\"video\"");
    if (key == std::string::npos) return {};
    size_t lb = body.find('[', key);
    if (lb == std::string::npos) return {};

    // 找到与之匹配的 ']'
    size_t rb = std::string::npos;
    int depth = 0;
    bool instr = false;
    for (size_t i = lb + 1; i < body.size(); ++i) {
        char c = body[i];
        if (instr) {
            if (c == '\\') { ++i; }
            else if (c == '"') instr = false;
            continue;
        }
        if (c == '"') instr = true;
        else if (c == '[' || c == '{') ++depth;
        else if (c == ']' || c == '}') {
            if (depth == 0) { rb = i; break; }
            --depth;
        }
    }
    if (rb == std::string::npos) return {};

    // 切分顶层对象
    std::vector<std::string> objs;
    size_t i = lb + 1;
    while (i < rb) {
        while (i < rb && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n' || body[i] == ',')) ++i;
        if (i >= rb) break;
        if (body[i] != '{') return {};
        size_t start = i;
        int d = 0;
        bool s = false;
        for (; i < rb; ++i) {
            char c = body[i];
            if (s) { if (c == '\\') ++i; else if (c == '"') s = false; continue; }
            if (c == '"') s = true;
            else if (c == '{') ++d;
            else if (c == '}') { --d; if (d == 0) { ++i; break; } }
        }
        objs.push_back(body.substr(start, i - start));
        if (d != 0) return {};
    }
    if (objs.empty()) return {};

    if (want == 0) return {};   // 自动：不改写

    // 严格只保留目标编码：退到别的编码（AV1/HEVC）在本机同样不能硬解，
    // 反而会“看着有流但播不动”。没有就返回空，由调用方去降清晰度重取。
    int keep = -1;
    for (auto& o : objs) {
        if (findCodecid(o) == want) { keep = want; break; }
    }
    if (keep < 0) return {};   // 该清晰度没有目标编码

    std::string arr = "[";
    bool first = true;
    for (auto& o : objs) {
        if (findCodecid(o) != keep) continue;
        if (!first) arr += ",";
        arr += o;
        first = false;
    }
    if (first) return {};
    arr += "]";

    std::string out;
    out.reserve(body.size());
    out.append(body, 0, lb);
    out += arr;
    out.append(body, rb + 1, std::string::npos);
    return out;
}

// ------------------------------------------------------------------ 网络


static const char* kNoLoginSnippet =
    "<script>(function(){if(window.__biliproxyNoLogin)return;window.__biliproxyNoLogin=1;"
    "var last=0;function isLogin(e){var n=e.target;while(n&&n.nodeType===1){if(/login|unlogin/i.test(String(n.className||'')))return true;n=n.parentNode;}return false;}['pointerdown','mousedown','keydown','touchstart'].forEach(function(t){"
    "document.addEventListener(t,function(e){last=Date.now()+(isLogin(e)?15000:0);},true);});"
    "function auto(){return Date.now()>last+1200;}"
    "function resume(){var v=document.querySelector('video');"
    "if(v&&v.paused&&!v.ended&&v.currentTime>0){var q=v.play();if(q&&q.catch)q.catch(function(){});}}"
    "function sweep(){if(!auto())return;"
    "var n=document.querySelectorAll('[class*=\"bili-mini\"],[class*=\"login-pwd-wp\"]');"
    "if(!n.length)return;for(var i=0;i<n.length;i++)n[i].remove();"
    "if(document.body)document.body.style.overflow='';resume();}"
    "new MutationObserver(function(){setTimeout(sweep,600);})"
    ".observe(document.documentElement,{childList:true,subtree:true});"
    "setInterval(sweep,1500);})();</script>";
std::string injectNoLogin(const std::string& html) {
    size_t p = html.find("</head>");
    if (p == std::string::npos) p = html.find("</HEAD>");
    if (p == std::string::npos) p = html.find("<body");
    if (p == std::string::npos) return html;
    std::string out;
    out.reserve(html.size() + 900);
    out.append(html, 0, p);
    out += kNoLoginSnippet;
    out.append(html, p, std::string::npos);
    return out;
}


std::string setQn(const std::string& target, int qn) {
    size_t p = target.find("qn=");
    if (p == std::string::npos) return target;
    size_t e = target.find('&', p);
    std::string out = target.substr(0, p) + "qn=" + std::to_string(qn);
    if (e != std::string::npos) out += target.substr(e);
    return out;
}

// fnval 位含义：1=mp4(非 dash) 16=dash 64=HDR 128=4K 256=杜比音 512=杜比视界 1024=8K 2048=AV1
std::string stripAv1Bit(const std::string& target) {
    size_t p = target.find("fnval=");
    if (p == std::string::npos) return target;
    size_t v = p + 6;
    size_t e = target.find('&', v);
    std::string num = target.substr(v, e == std::string::npos ? std::string::npos : e - v);
    if (num.empty()) return target;
    for (char c : num)
        if (c < '0' || c > '9') return target;
    unsigned long long val = strtoull(num.c_str(), nullptr, 10);
    if (!(val & 2048ULL)) return target;
    unsigned long long nv = val & ~2048ULL;
    std::string out = target.substr(0, v) + std::to_string(nv);
    if (e != std::string::npos) out += target.substr(e);
    return out;
}

static const char* kDownloadSnippet =
    "<script>(function(){if(window.__biliProxyDl)return;window.__biliProxyDl=1;"
    "if(!/\\/video\\//.test(location.pathname))return;"
    "var CSS='position:fixed;right:24px;bottom:96px;z-index:2147483600;background:#00a1d6;"
    "color:#fff;padding:10px 16px;border-radius:6px;cursor:pointer;font:14px/1 sans-serif;"
    "box-shadow:0 2px 8px rgba(0,0,0,.3);user-select:none';"
    "var btn=document.createElement('div');btn.textContent='下载';btn.style.cssText=CSS;"
    "document.documentElement.appendChild(btn);"
    "var panel=null,tm=null,st=null;"
    "function closePanel(){if(panel){panel.remove();panel=null;}}"
    "function makePanel(){"
    "var e=document.createElement('div');"
    "e.style.cssText='position:absolute;z-index:2147483600;background:#fff;border:1px solid #ddd;"
    "border-radius:8px;box-shadow:0 4px 16px rgba(0,0,0,.2);padding:10px;font:13px/1.6 sans-serif;"
    "color:#222;min-width:170px';"
    "e.innerHTML='<div style=\"font-weight:600;margin-bottom:6px\">下载视频</div>"
    "<div class=\"d0\" style=\"padding:6px 8px;cursor:pointer;border-radius:4px\">无水印</div>"
    "<div class=\"d1\" style=\"padding:6px 8px;cursor:pointer;border-radius:4px\">有水印</div>"
    "<div class=\"st\" style=\"margin-top:6px;color:#666;font-size:12px;max-width:260px;word-break:break-all\"></div>';"
    "var r=btn.getBoundingClientRect();"
    "e.style.left=(r.left+window.scrollX)+'px';"
    "e.style.top=(r.bottom+window.scrollY+6)+'px';"
    "document.body.appendChild(e);"
    "st=e.querySelector('.st');"
    "e.querySelector('.d0').onclick=function(){start(0);};"
    "e.querySelector('.d1').onclick=function(){start(1);};"
    "return e;}"
    "function start(wm){"
    "if(tm){clearInterval(tm);tm=null;}"
    "st.textContent='准备中…';"
    "var nm=encodeURIComponent((document.title||'').replace(/^【.*?】/,'').replace(/[\\\\\\/:*?\"<>|]/g,'_').slice(0,80)||'video');"
    "var done=false;"
    "function fail(t){if(done)return;done=true;st.textContent='失败：'+t;}"
    "var ctl=window.AbortController?new AbortController():null;"
    "var to=setTimeout(function(){if(ctl)ctl.abort();fail('超时');},8000);"
    "fetch('/__biliproxy/dl?wm='+wm+'&name='+nm,{cache:'no-store',signal:ctl?ctl.signal:undefined})"
    ".then(function(r){return r.json();})"
    ".then(function(j){"
    "clearTimeout(to);if(done)return;"
    "if(!j.ok){fail(j.msg||'未知');return;}"
    "st.textContent='下载中 0%';"
    "tm=setInterval(function(){"
    "fetch('/__biliproxy/dlstatus?id='+j.id,{cache:'no-store'})"
    ".then(function(r){return r.json();})"
    ".then(function(s){"
    "if(s.state==2){clearInterval(tm);tm=null;st.textContent='完成：'+(s.out||'');}"
    "else if(s.state==3){clearInterval(tm);tm=null;st.textContent='失败：'+(s.note||'');}"
    "else st.textContent='下载中 '+(s.pct||0)+'%';})"
    ".catch(function(){});},1000);})"
    ".catch(function(){clearTimeout(to);fail('请求失败');});}"
    "btn.onclick=function(e){e.stopPropagation();"
    "if(panel){closePanel();return;}"
    "panel=makePanel();};"
    "document.addEventListener('click',function(e){"
    "if(panel&&!panel.contains(e.target)&&e.target!==btn)closePanel();},true);"
    "})();</script>";
std::string injectDownload(const std::string& html) {
    size_t p = html.find("</body>");
    if (p == std::string::npos) return html;
    std::string out = html;
    out.insert(p, kDownloadSnippet);
    return out;
}

static const char* kFatalitySnippet =
    "<script>(function(){if(window.__biliProxyFatality)return;window.__biliProxyFatality=1;"
    "var d=document.createElement('div');"
    "d.style.cssText='position:fixed;left:0;top:0;width:100vw;height:100vh;z-index:2147483647;"
    "pointer-events:none;background:transparent;';"
    "var f=document.createElement('iframe');"
    "f.src='/__biliproxy/fatality.html?clear=1&loop=0';"
    "f.setAttribute('allowtransparency','true');"
    "f.setAttribute('scrolling','no');"
    "f.style.cssText='position:absolute;left:0;top:0;width:100%;height:100%;"
    "border:0;margin:0;padding:0;background:transparent;display:block;';"
    "f.onload=function(){setTimeout(function(){d.remove();},3200);};"
    "d.appendChild(f);document.documentElement.appendChild(d);})();</script>";
std::string injectFatality(const std::string& html) {
    size_t p = html.find("</body>");
    if (p == std::string::npos) p = html.find("</html>");
    if (p == std::string::npos) return html + kFatalitySnippet;
    std::string out = html;
    out.insert(p, kFatalitySnippet);
    return out;
}