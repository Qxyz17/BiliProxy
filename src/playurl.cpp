#include "common.h"
#include "playurl.h"
#include "taster_html.h"

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
    "var st=document.createElement('style');"
    "st.textContent='"
    "@keyframes bpIn{from{transform:translateY(-50%) translateX(100%);opacity:0}"
    "to{transform:translateY(-50%) translateX(0);opacity:1}}"
    "@media (prefers-reduced-motion:reduce){@keyframes bpIn{from{opacity:0}to{opacity:1}}}"
    "#bpDl{position:fixed;right:0;top:50%;transform:translateY(-50%);"
    "z-index:2147483600;background:#00a1d6;color:#fff;padding:16px 12px;"
    "border-radius:8px 0 0 8px;cursor:pointer;font:14px/1.6 sans-serif;"
    "letter-spacing:4px;writing-mode:vertical-rl;user-select:none;"
    "overflow:hidden;animation:bpIn .5s ease-out .6s backwards;"
    "transition:background .18s ease,transform .18s ease,box-shadow .18s ease}"
    "#bpDl:hover{background:#0090c8;transform:translateY(-50%) translateX(-4px);"
    "box-shadow:-4px 2px 12px rgba(0,0,0,.22)}"
    "#bpDl:active{background:#0082b3;transform:translateY(-50%) translateX(-1px);"
    "transition-duration:.06s}"
    "#bpDl .bpF{position:absolute;left:0;right:0;bottom:0;height:0;"
    "background:rgba(255,255,255,.22);transition:height .35s ease;pointer-events:none}"
    "#bpDl span{position:relative}';"
    "document.head.appendChild(st);"
    "var btn=document.createElement('div');btn.id='bpDl';"
    "btn.innerHTML='<div class=\"bpF\"></div><span>下载</span>';"
    "document.documentElement.appendChild(btn);"
    "var fill=btn.querySelector('.bpF');"
    "var panel=null,tm=null,st2=null,bar=null;"
    "function closePanel(){if(panel){panel.remove();panel=null;}}"
    "function makePanel(){"
    "var e=document.createElement('div');"
    "e.style.cssText='position:fixed;z-index:2147483600;background:#fff;"
    "border:1px solid #e3e3e3;border-radius:8px;padding:10px;"
    "box-shadow:0 2px 10px rgba(0,0,0,.12);font:13px/1.6 sans-serif;"
    "color:#222;width:180px;box-sizing:border-box;"
    "opacity:0;transform:translateX(6px);transition:opacity .16s ease,transform .16s ease';"
    "e.innerHTML='<div style=\"font-weight:600;margin-bottom:6px\">下载视频</div>"
    "<div class=\"d0\" style=\"padding:6px 8px;cursor:pointer;border-radius:4px;"
    "transition:background .12s\">无水印</div>"
    "<div class=\"d1\" style=\"padding:6px 8px;cursor:pointer;border-radius:4px;"
    "transition:background .12s\">有水印</div>"
    "<div class=\"bw\" style=\"display:none;margin-top:8px;height:4px;background:#eee;"
    "border-radius:2px;overflow:hidden\"><div class=\"bar\" style=\"height:100%;width:0;"
    "background:#00a1d6;transition:width .3s ease\"></div></div>"
    "<div class=\"st\" style=\"margin-top:6px;color:#666;font-size:12px;"
    "word-break:break-all\"></div>';"
    "document.body.appendChild(e);"
    "var opts=e.querySelectorAll('.d0,.d1');"
    "for(var i=0;i<opts.length;i++){"
    "opts[i].addEventListener('mouseover',function(){this.style.background='#f4f4f4';});"
    "opts[i].addEventListener('mouseout',function(){this.style.background='';});}"
    "var r=btn.getBoundingClientRect();"
    "var pw=180,ph=e.offsetHeight||130,vh=window.innerHeight;"
    "var left=r.left-pw-8;"
    "if(left<8)left=8;"
    "var top=r.top;"
    "if(top+ph>vh-8)top=vh-ph-8;"
    "if(top<8)top=8;"
    "e.style.left=left+'px';e.style.top=top+'px';"
    "requestAnimationFrame(function(){e.style.opacity='1';e.style.transform='translateX(0)';});"
    "st2=e.querySelector('.st');bar=e.querySelector('.bar');"
    "e.querySelector('.d0').onclick=function(){start(0);};"
    "e.querySelector('.d1').onclick=function(){start(1);};"
    "return e;}"
    "function start(wm){"
    "if(tm){clearInterval(tm);tm=null;}"
    "st2.textContent='准备中…';bar.parentNode.style.display='none';"
    "var vd=window.__INITIAL_STATE__&&window.__INITIAL_STATE__.videoData;"
    "var aid=vd&&vd.aid||'';var cid=vd&&vd.cid||'';"
    "var nm=encodeURIComponent((document.title||'').replace(/^【.*?】/,'')"
    ".replace(/_哔哩哔哩_bilibili$/,'')"
    ".replace(/[\\\\\\/:*?\"<>|]/g,'_').slice(0,80)||'video');"
    "var done=false;"
    "function fail(t){if(done)return;done=true;"
    "st2.textContent='失败：'+t;bar.parentNode.style.display='none';fill.style.height='0';}"
    "var ctl=window.AbortController?new AbortController():null;"
    "var to=setTimeout(function(){if(ctl)ctl.abort();fail('超时');},15000);"
    "fetch('/__biliproxy/dl?wm='+wm+'&name='+nm+'&aid='+aid+'&cid='+cid,{cache:'no-store',signal:ctl?ctl.signal:undefined})"
    ".then(function(r){return r.json();})"
    ".then(function(j){"
    "clearTimeout(to);if(done)return;"
    "if(!j.ok){fail(j.msg||'未知');return;}"
    "bar.parentNode.style.display='block';bar.style.width='0%';"
    "st2.textContent='下载中 0%';"
    "tm=setInterval(function(){"
    "fetch('/__biliproxy/dlstatus?id='+j.id,{cache:'no-store'})"
    ".then(function(r){return r.json();})"
    ".then(function(s){"
    "if(s.state==2){clearInterval(tm);tm=null;bar.style.width='100%';"
    "fill.style.height='100%';setTimeout(function(){fill.style.height='0';},1800);"
    "st2.textContent='完成：'+(s.out||'').split(/[\\\\/]/).pop();}"
    "else if(s.state==3){clearInterval(tm);tm=null;fail(s.note||'失败');}"
    "else{bar.style.width=(s.pct||0)+'%';fill.style.height=(s.pct||0)+'%';"
    "st2.textContent='下载中 '+(s.pct||0)+'%';}})"
    ".catch(function(){});},500);})"
    ".catch(function(){clearTimeout(to);fail('请求失败');});}"
    "btn.onclick=function(e){e.stopPropagation();"
    "if(panel){closePanel();return;}"
    "panel=makePanel();};"
    "document.addEventListener('click',function(e){"
    "if(panel&&!panel.contains(e.target)&&e.target!==btn)closePanel();},true);"
    "window.addEventListener('resize',function(){if(panel)closePanel();});"
    "})();</script>";

std::string injectDownload(const std::string& html) {
    size_t p = html.find("</body>");
    if (p == std::string::npos) return html;
    std::string out = html;
    out.insert(p, kDownloadSnippet);
    return out;
}

// 页面里没有 chrome.* 扩展 API，用 localStorage + 代理端点和原始 content.js 的调用对齐
static const char* kTasterShim =
    "<script>(function(){if(window.__btShim)return;window.__btShim=1;"
    "var C=window.chrome||(window.chrome={});"
    "var S=C.storage||(C.storage={});"
    "var L=S.local||(S.local={});"
    "function K(k){return 'bt:'+k;}"
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
    "if(!msg)return done({ok:false,error:'empty'});"
    "if(msg.type==='BILI_FETCH'){"
    "fetch('/__biliproxy/fetch?url='+encodeURIComponent(msg.url),{credentials:'include'})"
    ".then(function(r){return r.json();}).then(done)"
    ".catch(function(e){done({ok:false,error:String(e&&e.message||e)});});return;}"
    "if(msg.type==='AI_FETCH'){"
    "fetch('/__biliproxy/ai',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({url:msg.url,headers:msg.headers||{},body:msg.body||''})})"
    ".then(function(r){return r.json();}).then(done)"
    ".catch(function(e){done({ok:false,error:String(e&&e.message||e)});});return;}"
    "done({ok:false,error:'unsupported'});};"
    "})();</script>";

std::string injectTaster(const std::string& html) {
    size_t p = html.find("</body>");
    if (p == std::string::npos) p = html.find("</html>");
    if (p == std::string::npos) return html;
    std::string blk = std::string("<style>") + kTasterCss + "</style>" +
                      kTasterShim +
                      std::string("<script>") + kTasterJs + "</script>";
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