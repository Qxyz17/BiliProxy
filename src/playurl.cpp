#include "common.h"
#include "playurl.h"
#include "webui_html.h"

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


// 只隐藏弹窗，不从 DOM 里摘除：B 站的播放器挂在同一棵树上，删节点会把播放状态一起带走
static const char* kNoLoginSnippet =
    "<script>(function(){if(window.__biliproxyNoLogin)return;window.__biliproxyNoLogin=1;"
    "var last=0,styled=false;"
    "function style(){if(styled)return;styled=true;var s=document.createElement('style');"
    "s.textContent='.__bpNoLogin{display:none!important;visibility:hidden!important;"
    "pointer-events:none!important}';"
    "(document.head||document.documentElement).appendChild(s);}"
    "function isLogin(e){var n=e.target;while(n&&n.nodeType===1){"
    "if(/login|unlogin/i.test(String(n.className||'')))return true;n=n.parentNode;}return false;}"
    "['pointerdown','mousedown','keydown','touchstart'].forEach(function(t){"
    "document.addEventListener(t,function(e){last=Date.now()+(isLogin(e)?15000:0);},true);});"
    "function auto(){return Date.now()>last+1200;}"
    "function resume(){var v=document.querySelector('video');"
    "if(!v||!v.paused||v.ended)return;"
    "if(!(v.currentTime>0))return;"
    "var q=v.play();if(q&&q.catch)q.catch(function(){});}"
    "function sweep(){if(!auto())return;style();"
    "var n=document.querySelectorAll('[class*=\"bili-mini\"],[class*=\"login-pwd-wp\"]');"
    "for(var i=0;i<n.length;i++){var el=n[i];"
    "if(el.className.indexOf('__bpNoLogin')<0)el.className+=' __bpNoLogin';}"
    "if(n.length&&document.body)document.body.style.overflow='';}"
    "function tick(){sweep();"
    "var v=document.querySelector('video');"
    "if(v&&v.paused&&!v.ended&&v.currentTime>0)resume();}"
    "if(document.documentElement)new MutationObserver(function(){setTimeout(tick,400);})"
    ".observe(document.documentElement,{childList:true,subtree:true});"
    "setInterval(tick,1200);})();</script>";
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

// 页面里没有扩展 API，用 localStorage + 代理端点顶上
static const char* kWebUiShim =
    "<script>(function(){if(window.__bpShim)return;window.__bpShim=1;"
    "var C=window.chrome||(window.chrome={});"
    "var S=C.storage||(C.storage={});"
    "var L=S.local||(S.local={});"
    "function K(k){return 'bp:'+k;}"
    "L.get=function(keys,cb){var out={};"
    "var list=Array.isArray(keys)?keys:(typeof keys==='string'?[keys]:(keys?Object.keys(keys):[]));"
    "try{for(var i=0;i<list.length;i++){var v=localStorage.getItem(K(list[i]));"
    "if(v!==null)out[list[i]]=JSON.parse(v);}}catch(e){}"
    "setTimeout(function(){cb(out);},0);};"
    "L.set=function(obj,cb){try{for(var k in obj){"
    "if(Object.prototype.hasOwnProperty.call(obj,k))localStorage.setItem(K(k),JSON.stringify(obj[k]));}}catch(e){}"
    "setTimeout(function(){cb&&cb();},0);};"
    "var R=C.runtime||(C.runtime={});"
    "R.lastError=undefined;"
    "R.sendMessage=function(msg,cb){cb=cb||function(){};"
    "var done=function(r){setTimeout(function(){cb(r);},0);};"
    "done({ok:false,error:'unsupported'});};"
    "})();</script>";

std::string injectWebUi(const std::string& html) {
    size_t p = html.find("</body>");
    if (p == std::string::npos) p = html.find("</html>");
    if (p == std::string::npos) return html;
    std::string blk = std::string("<style>") + kWebUiCss + "</style>" +
                      kWebUiShim +
                      std::string("<script>") + kWebUiJs + "</script>";
    std::string out = html;
    out.insert(p, blk);
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