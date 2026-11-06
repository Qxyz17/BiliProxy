// BiliProxy 页面侧边栏：下载 + AI 对话共用一个入口
(function () {
  "use strict";
  if (window.__biliProxyUi) return;
  window.__biliProxyUi = 1;
  if (!/\/video\//.test(location.pathname)) return;

  var $ = function (s, r) { return (r || document).querySelector(s); };

  var state = {
    cfg: { base: "", key: "", model: "" },
    opts: null,
    open: false,
    history: [],
    ctx: null,
    dl: null,
  };

  var DEFAULTS = {
    title: true,
    desc: true,
    owner: true,
    subtitle: true,
    danmaku: false,
    comments: false,
    frameMode: "permin",
    frameFixed: 24,
    framePerMin: 6,
    frameWidth: 384,
    subtitleLimit: 12000,
    danmakuCount: 200,
    commentCount: 20,
    systemPrompt: "你是视频内容分析助手，用中文简明作答。",
    extraPrompt: "",
  };

  var ICON = {
    chat: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M21 11.5a8.4 8.4 0 0 1-9 8.4 8.9 8.9 0 0 1-4-.9L3 21l1.9-4.9a8.4 8.4 0 0 1-1-4A8.4 8.4 0 0 1 12.9 3 8.4 8.4 0 0 1 21 11.5z"/></svg>',
    down: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3v12"/><path d="m7 11 5 5 5-5"/><path d="M4 20h16"/></svg>',
    gear: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.6 1.6 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.6 1.6 0 0 0-1.8-.3 1.6 1.6 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.6 1.6 0 0 0-1-1.5 1.6 1.6 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.6 1.6 0 0 0 .3-1.8 1.6 1.6 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.6 1.6 0 0 0 1.5-1 1.6 1.6 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.6 1.6 0 0 0 1.8.3H9a1.6 1.6 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.6 1.6 0 0 0 1 1.5 1.6 1.6 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.6 1.6 0 0 0-.3 1.8V9a1.6 1.6 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.6 1.6 0 0 0-1.5 1z"/></svg>',
    close: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>',
    back: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M15 18l-6-6 6-6"/></svg>',
  };

  /* ================= 数据 ================= */

  // 注入的脚本就在页面上下文，直接读全局
  function pageState() {
    try {
      var st = window.__INITIAL_STATE__;
      if (!st) return null;
      return { videoData: st.videoData, p: st.p };
    } catch (e) { return null; }
  }

  function bvid() {
    var m = location.pathname.match(/\/video\/(BV[0-9A-Za-z]+)/);
    return m ? m[1] : "";
  }

  function apiFetch(url) {
    return fetch("/__biliproxy/fetch?url=" + encodeURIComponent(url), { credentials: "include", cache: "no-store" })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        if (!j || !j.ok) return null;
        try { return JSON.parse(j.text); } catch (e) { return null; }
      })
      .catch(function () { return null; });
  }

  function apiRaw(url) {
    return fetch("/__biliproxy/fetch?url=" + encodeURIComponent(url), { credentials: "include", cache: "no-store" })
      .then(function (r) { return r.json(); })
      .then(function (j) { return j && j.ok ? j.text : ""; })
      .catch(function () { return ""; });
  }

  function getInfo() {
    return getInfoInner().catch(function () {
      return { bvid: bvid(), aid: "", cid: "", title: document.title.replace(/_哔哩哔哩.*$/, "").trim(),
               desc: "", owner: "", duration: 0, stat: null, url: location.href };
    });
  }

  async function getInfoInner() {
    var info = {
      bvid: bvid(), aid: "", cid: "", title: document.title.replace(/_哔哩哔哩.*$/, "").trim(),
      desc: "", owner: "", duration: 0, stat: null, url: location.href,
    };
    var st = pageState();
    if (st && st.videoData) {
      var vd = st.videoData;
      info.aid = vd.aid || "";
      info.title = vd.title || info.title;
      info.desc = vd.desc || "";
      info.duration = vd.duration || 0;
      info.owner = vd.owner && vd.owner.name || "";
      info.stat = vd.stat || null;
      if (st.p && vd.pages) {
        for (var i = 0; i < vd.pages.length; i++) {
          if (vd.pages[i].page === st.p) { info.cid = vd.pages[i].cid; break; }
        }
      }
      if (!info.cid) info.cid = vd.cid || "";
    }
    if (info.bvid && (!info.aid || !info.cid)) {
      var d = await apiFetch("https://api.bilibili.com/x/web-interface/view?bvid=" + info.bvid);
      if (d && d.code === 0 && d.data) {
        info.aid = info.aid || d.data.aid;
        info.cid = info.cid || d.data.cid;
        info.title = info.title || d.data.title;
        info.desc = info.desc || d.data.desc;
        info.owner = info.owner || (d.data.owner && d.data.owner.name) || "";
        info.duration = info.duration || d.data.duration;
        info.stat = info.stat || d.data.stat;
      }
    }
    return info;
  }

  function storageGet(keys) {
    return new Promise(function (resolve) {
      try { chrome.storage.local.get(keys, function (v) { resolve(v || {}); }); }
      catch (e) { resolve({}); }
    });
  }
  function storageSet(obj) {
    return new Promise(function (resolve, reject) {
      try {
        chrome.storage.local.set(obj, function () {
          if (chrome.runtime && chrome.runtime.lastError) reject(new Error(chrome.runtime.lastError.message));
          else resolve();
        });
      } catch (e) { reject(e); }
    });
  }

  function getDanmaku(info) {
    if (!info.cid) return Promise.resolve("");
    return apiRaw("https://comment.bilibili.com/" + info.cid + ".xml").then(function (xml) {
      if (!xml) return "";
      var re = /<d p="[^"]*">([\s\S]*?)<\/d>/g, m, list = [];
      while ((m = re.exec(xml))) {
        var t = m[1].replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&").replace(/&quot;/g, '"');
        if (t) list.push(t);
      }
      if (!list.length) return "";
      var n = Math.min(state.opts.danmakuCount, list.length);
      var step = list.length / n, pick = [];
      for (var i = 0; i < n; i++) pick.push(list[Math.floor(i * step)]);
      return pick.join(" / ");
    }).catch(function () { return ""; });
  }

  function getComments(info) {
    if (!info.aid) return Promise.resolve("");
    return apiFetch("https://api.bilibili.com/x/v2/reply?type=1&oid=" + info.aid + "&sort=2&ps=" +
                    Math.max(1, Math.min(49, state.opts.commentCount)) + "&pn=1").then(function (d) {
      var rs = d && d.data && d.data.replies;
      if (!rs || !rs.length) return "";
      return rs.map(function (r) {
        var txt = (r.content && r.content.message || "").replace(/\s+/g, " ").trim();
        return txt ? (txt + "（赞 " + (r.like || 0) + "）") : "";
      }).filter(Boolean).join("\n");
    }).catch(function () { return ""; });
  }

  function getSubtitles(info) {
    var dom = grabSubtitleDom();
    if (dom) return Promise.resolve(dom);
    if (!info.bvid || !info.cid) return Promise.resolve("");
    return apiFetch("https://api.bilibili.com/x/player/v2?bvid=" + info.bvid + "&cid=" + info.cid).then(function (d) {
      var subs = d && d.data && d.data.subtitle && d.data.subtitle.subtitles;
      if (!subs || !subs.length) return "";
      var pick = null;
      for (var i = 0; i < subs.length; i++) {
        if (/zh|cn|ai/i.test(subs[i].lan || "")) { pick = subs[i]; break; }
      }
      pick = pick || subs[0];
      var u = pick.subtitle_url || pick.url || "";
      if (u.indexOf("//") === 0) u = "https:" + u;
      if (!u) return "";
      return apiFetch(u).then(function (j) {
        var body = j && j.body || [];
        return body.map(function (x) { return x.content; }).join("\n");
      });
    }).catch(function () { return ""; });
  }

  function grabSubtitleDom() {
    var ns = document.querySelectorAll(".bpx-player-subtitle-panel-text, .subtitle-item-text");
    if (!ns.length) return "";
    return Array.prototype.map.call(ns, function (n) { return n.textContent.trim(); })
      .filter(Boolean).join("\n");
  }

  function frameTotal(duration) {
    var o = state.opts;
    if (o.frameMode === "fixed") return Math.max(1, Math.min(120, o.frameFixed | 0));
    var per = Math.max(0.1, o.framePerMin || 6);
    return Math.max(1, Math.min(120, Math.ceil(duration / 60 * per)));
  }

  // 抽帧交给代理：直接用下载用的那条流，不碰页面播放器，也不受跳转影响
  async function captureFrames(info) {
    var o = state.opts;
    var n = frameTotal(info.duration || 0);
    var w = Math.max(160, Math.min(768, o.frameWidth || 384));
    var r = await fetch("/__biliproxy/frames?cid=" + encodeURIComponent(info.cid || "") +
                        "&n=" + n + "&w=" + w, { cache: "no-store" });
    var j = await r.json();
    if (!j || !j.ok) throw new Error(j && j.msg || "抽帧失败");
    var out = [];
    var step = (info.duration || 0) / (j.frames.length || 1);
    for (var i = 0; i < j.frames.length; i++) out.push({ url: j.frames[i], t: step * (i + 0.5) });
    return out;
  }

  /* ================= 提示词 ================= */

  function fmt(t) {
    if (!isFinite(t)) return "?";
    var m = Math.floor(t / 60), s = Math.floor(t % 60);
    return m + ":" + (s < 10 ? "0" : "") + s;
  }

  function buildPrompt(ctx) {
    var o = state.opts, info = ctx.info, p = [];
    p.push("解析下面这个 B 站视频，尽量基于给全的材料作答，不要臆测没给到的内容。");
    p.push("链接：" + (info.url || ""));
    if (o.title) p.push("标题：" + (info.title || ""));
    if (o.owner) p.push("作者：" + (info.owner || ""));
    if (o.desc) {
      p.push("简介：" + (info.desc ? info.desc.slice(0, 2000) : "(无)"));
      if (info.stat) {
        p.push("数据：播放 " + (info.stat.view || 0) + "，点赞 " + (info.stat.like || 0) +
               "，投币 " + (info.stat.coin || 0) + "，收藏 " + (info.stat.favorite || 0) +
               "，弹幕 " + (info.stat.danmaku || 0) + "，评论 " + (info.stat.reply || 0));
      }
      if (info.duration) p.push("时长：" + fmt(info.duration));
    }
    if (ctx.subtitle) {
      p.push("");
      p.push("【字幕/文字稿】");
      p.push(ctx.subtitle.slice(0, o.subtitleLimit));
      if (ctx.subtitle.length > o.subtitleLimit) p.push("（字幕过长，已按上限截断）");
    }
    if (ctx.danmaku) {
      p.push("");
      p.push("【弹幕抽样 " + o.danmakuCount + " 条，反映观众实时反应】");
      p.push(ctx.danmaku);
    }
    if (ctx.comments) {
      p.push("");
      p.push("【评论区热评】");
      p.push(ctx.comments);
    }
    if (o.extraPrompt) {
      p.push("");
      p.push(o.extraPrompt);
    }
    return p.join("\n");
  }

  function buildUserContent(prompt, frames) {
    if (!frames || !frames.length) return prompt;
    var o = state.opts;
    var times = frames.map(function (f) { return fmt(f.t); }).join("、");
    var note = prompt + "\n\n另附 " + frames.length + " 张按时间抽取的画面帧，顺序对应时间点：" + times +
      "。帧是稀疏采样，只用于辅助判断画面内容，不能替代字幕/文字稿对全程的描述。";
    var content = [{ type: "text", text: note }];
    for (var i = 0; i < frames.length; i++) content.push({ type: "image_url", image_url: { url: frames[i].url } });
    return content;
  }

  /* ================= AI ================= */

  function normBase(b) { return (b || "").trim().replace(/\/+$/, ""); }
  function kindOf(b) { return /gemini/i.test(b) ? "gemini" : "openai"; }

  function aiPost(url, headers, bodyObj) {
    return fetch("/__biliproxy/ai", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ url: url, headers: headers, body: JSON.stringify(bodyObj) }),
    }).then(function (r) { return r.json(); });
  }

  async function callAI(messages) {
    var c = state.cfg;
    if (c.kind === "gemini") {
      var sys = messages.filter(function (m) { return m.role === "system"; })
        .map(function (m) { return m.content; }).join("\n");
      var contents = messages.filter(function (m) { return m.role !== "system"; }).map(function (m) {
        return {
          role: m.role === "assistant" ? "model" : "user",
          parts: typeof m.content === "string" ? [{ text: m.content }] : m.content.map(function (x) {
            if (x.type === "text") return { text: x.text };
            var mm = String(x.image_url && x.image_url.url || "").match(/^data:(.+?);base64,(.*)$/);
            return mm ? { inlineData: { mimeType: mm[1], data: mm[2] } } : { text: "" };
          }),
        };
      });
      var gb = { contents: contents };
      if (sys) gb.systemInstruction = { parts: [{ text: sys }] };
      var gr = await aiPost(c.base + "/v1beta/models/" + c.model + ":generateContent?key=" + encodeURIComponent(c.key),
                            { "Content-Type": "application/json" }, gb);
      if (!gr || !gr.ok) throw new Error("接口错误 " + (gr ? gr.status + " " + String(gr.text || "").slice(0, 200) : "无响应"));
      var gj = JSON.parse(gr.text);
      var parts = (gj.candidates && gj.candidates[0] && gj.candidates[0].content && gj.candidates[0].content.parts) || [];
      return parts.map(function (x) { return x.text || ""; }).join("") || "(空回复)";
    }
    var r = await aiPost(c.base + "/chat/completions",
      { "Content-Type": "application/json", Authorization: "Bearer " + c.key },
      { model: c.model, messages: messages, stream: false });
    if (!r || !r.ok) throw new Error("接口错误 " + (r ? r.status + " " + String(r.text || "").slice(0, 200) : "无响应"));
    var j = JSON.parse(r.text);
    return (j.choices && j.choices[0] && j.choices[0].message && j.choices[0].message.content) || "(空回复)";
  }

  function hasImages(messages) {
    for (var i = 0; i < messages.length; i++) {
      if (Array.isArray(messages[i].content)) return true;
    }
    return false;
  }

  // 只有错误里真的提到图片/视觉才降级，普通 400 不能当成不支持图片
  function isImageReject(msg) {
    return /image|vision|multimodal|image_url|含图|图片|图像/i.test(msg);
  }

  // 模型不收图时会整条拒绝，降级成只要文字重发一次
  function dropImages(messages) {
    return messages.map(function (m) {
      if (!Array.isArray(m.content)) return m;
      var txt = "";
      for (var i = 0; i < m.content.length; i++) {
        if (m.content[i].type === "text") txt += m.content[i].text;
      }
      return { role: m.role, content: txt };
    });
  }

  async function ask(messages) {
    try {
      return await callAI(messages);
    } catch (e) {
      var msg = String(e && e.message || e);
      if (hasImages(messages) && isImageReject(msg)) {
        addMsg("sys", "接口拒绝了图片输入，改为只发文字重试");
        return await callAI(dropImages(messages));
      }
      throw e;
    }
  }

  /* ================= UI ================= */

  function el(tag, cls, html) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (html != null) e.innerHTML = html;
    return e;
  }

  function buildRail() {
    var rail = el("div");
    rail.id = "bpRail";
    rail.appendChild(mkBtn("chat", "对话", ICON.chat, function () { togglePanel(); }));
    rail.appendChild(mkBtn("down", "下载", ICON.down, function () { toggleDl(); }));
    document.documentElement.appendChild(rail);
    return rail;
  }

  function mkBtn(key, label, icon, onclick) {
    var b = el("div", "bpBtn", '<i class="fill"></i>' + icon + '<span class="lb">' + label + "</span>");
    b.setAttribute("data-k", key);
    b.onclick = function (e) { e.stopPropagation(); onclick(); };
    return b;
  }

  function buildPanel() {
    var p = el("div");
    p.id = "bpPanel";
    p.innerHTML =
      '<div class="bpHead"><span class="bpBrand">Bili<em>Proxy</em></span>' +
      '<button class="bpIcon" data-act="settings" title="设置">' + ICON.gear + "</button>" +
      '<button class="bpIcon" data-act="close" title="收起">' + ICON.close + "</button></div>" +
      '<div class="bpVid"><div class="bpVtitle">…</div><div class="bpVmeta"></div>' +
      '<button class="bpPrimary" data-act="sum">总结这个视频</button></div>' +
      '<div class="bpChat"></div>' +
      '<div class="bpComposer"><textarea class="bpInput" rows="1" placeholder="就这个视频问点什么…"></textarea>' +
      '<button class="bpPrimary" data-act="send">发送</button></div>' +
      '<div class="bpSet"></div>';
    document.documentElement.appendChild(p);

    var head = $(".bpHead", p);
    head.addEventListener("click", function (e) {
      var act = e.target.closest("[data-act]");
      if (!act) return;
      if (act.getAttribute("data-act") === "settings") openSettings(true);
      if (act.getAttribute("data-act") === "close") togglePanel(false);
    });
    $(".bpVid", p).addEventListener("click", function (e) {
      if (e.target.closest('[data-act="sum"]')) summarize();
    });
    $(".bpComposer", p).addEventListener("click", function (e) {
      if (e.target.closest('[data-act="send"]')) send();
    });
    $(".bpInput", p).addEventListener("keydown", function (e) {
      if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); send(); }
    });
    buildSettings($(".bpSet", p));
    return p;
  }

  function openSettings(show) {
    var s = $("#bpPanel .bpSet");
    if (!s) return;
    s.classList.toggle("open", show !== false);
    if (show !== false) fillSettings();
    var b = $('#bpRail .bpBtn[data-k="chat"]');
    if (b) b.classList.add("on");
  }

  function buildSettings(root) {
    root.innerHTML =
      '<div class="bpSetHead"><button class="bpIcon" data-act="back" title="返回">' + ICON.back + "</button>" +
      "<b>设置</b></div>" +
      '<div class="bpSec"><h4>接口</h4>' +
      '<label class="bpRow"><span>Base URL</span><input class="bpField s-base" placeholder="https://api.openai.com/v1"></label>' +
      '<label class="bpRow"><span>API Key</span><input class="bpField s-key" type="password" placeholder="sk-..."></label>' +
      '<label class="bpRow"><span>模型</span><input class="bpField s-model" placeholder="gpt-4o / gemini-2.0-flash"></label>' +
      "</div>" +
      '<div class="bpSec"><h4>发给 AI 的内容</h4>' +
      '<div class="bpChecks">' +
      '<label class="bpChk"><input type="checkbox" class="k-title">标题</label>' +
      '<label class="bpChk"><input type="checkbox" class="k-owner">作者</label>' +
      '<label class="bpChk"><input type="checkbox" class="k-desc">简介与数据</label>' +
      '<label class="bpChk"><input type="checkbox" class="k-subtitle">字幕</label>' +
      '<label class="bpChk"><input type="checkbox" class="k-danmaku">弹幕</label>' +
      '<label class="bpChk"><input type="checkbox" class="k-comments">评论</label>' +
      "</div></div>" +
      '<div class="bpSec"><h4>画面帧</h4>' +
      '<div class="bpGrid">' +
      '<label class="bpRow"><span>取样方式</span><select class="bpField s-fmode">' +
      '<option value="permin">按每分钟帧数</option><option value="fixed">固定总帧数</option></select></label>' +
      '<label class="bpRow"><span>帧/m <i>每分钟</i></span><input class="bpField s-fpm" type="number" min="1" max="60" step="1"></label>' +
      '<label class="bpRow"><span>总帧数 <i>固定模式</i></span><input class="bpField s-fixed" type="number" min="1" max="120" step="1"></label>' +
      '<label class="bpRow"><span>帧宽 <i>像素</i></span><input class="bpField s-fw" type="number" min="160" max="768" step="64"></label>' +
      "</div></div>" +
      '<div class="bpSec"><h4>条数上限</h4>' +
      '<div class="bpGrid">' +
      '<label class="bpRow"><span>字幕字数</span><input class="bpField s-sublim" type="number" min="500" max="60000" step="500"></label>' +
      '<label class="bpRow"><span>弹幕条数</span><input class="bpField s-dmnum" type="number" min="20" max="2000" step="20"></label>' +
      '<label class="bpRow"><span>评论条数</span><input class="bpField s-cmnum" type="number" min="1" max="49" step="1"></label>' +
      "</div></div>" +
      '<div class="bpSec"><h4>提示词</h4>' +
      '<label class="bpRow"><span>系统提示词</span><textarea class="bpField s-sys" rows="3"></textarea></label>' +
      '<label class="bpRow"><span>附加要求 <i>拼在材料末尾</i></span><textarea class="bpField s-extra" rows="3"></textarea></label>' +
      "</div>" +
      '<div class="bpFoot"><button class="bpPrimary" data-act="save">保存</button>' +
      '<button class="bpGhost" data-act="reset">恢复默认</button>' +
      '<span class="bpStatus"></span></div>';

    root.addEventListener("click", function (e) {
      var act = e.target.closest("[data-act]");
      if (!act) return;
      var a = act.getAttribute("data-act");
      if (a === "back") openSettings(false);
      if (a === "save") saveSettings();
      if (a === "reset") resetSettings();
    });
    var sync = function () {
      Array.prototype.forEach.call(root.querySelectorAll(".bpChk"), function (l) {
        var cb = l.querySelector("input");
        l.classList.toggle("on", !!cb.checked);
      });
    };
    root.addEventListener("change", function (e) {
      if (e.target.type === "checkbox") sync();
    });
    root.addEventListener("click", function (e) {
      var l = e.target.closest(".bpChk");
      if (l && e.target !== l && e.target.tagName !== "INPUT") {
        var cb = l.querySelector("input");
        cb.checked = !cb.checked;
        sync();
      }
    });
    root.__sync = sync;
  }

  function fillSettings() {
    var r = $("#bpPanel .bpSet");
    if (!r) return;
    var o = state.opts, c = state.cfg;
    $(".s-base", r).value = c.base || "";
    $(".s-key", r).value = c.key || "";
    $(".s-model", r).value = c.model || "";
    [["k-title", "title"], ["k-owner", "owner"], ["k-desc", "desc"],
     ["k-subtitle", "subtitle"], ["k-danmaku", "danmaku"], ["k-comments", "comments"]]
      .forEach(function (kv) { $("." + kv[0], r).checked = !!o[kv[1]]; });
    $(".s-fmode", r).value = o.frameMode;
    $(".s-fpm", r).value = o.framePerMin;
    $(".s-fixed", r).value = o.frameFixed;
    $(".s-fw", r).value = o.frameWidth;
    $(".s-sublim", r).value = o.subtitleLimit;
    $(".s-dmnum", r).value = o.danmakuCount;
    $(".s-cmnum", r).value = o.commentCount;
    $(".s-sys", r).value = o.systemPrompt;
    $(".s-extra", r).value = o.extraPrompt;
    if (r.__sync) r.__sync();
  }

  function readSettings() {
    var r = $("#bpPanel .bpSet");
    var num = function (sel, def, lo, hi) {
      var v = parseInt($(sel, r).value, 10);
      if (!isFinite(v)) return def;
      return Math.max(lo, Math.min(hi, v));
    };
    return {
      cfg: {
        base: normBase($(".s-base", r).value),
        key: $(".s-key", r).value.trim(),
        model: $(".s-model", r).value.trim(),
      },
      opts: {
        title: $(".k-title", r).checked,
        owner: $(".k-owner", r).checked,
        desc: $(".k-desc", r).checked,
        subtitle: $(".k-subtitle", r).checked,
        danmaku: $(".k-danmaku", r).checked,
        comments: $(".k-comments", r).checked,
        frameMode: $(".s-fmode", r).value,
        framePerMin: num(".s-fpm", 6, 1, 60),
        frameFixed: num(".s-fixed", 24, 1, 120),
        frameWidth: num(".s-fw", 384, 160, 768),
        subtitleLimit: num(".s-sublim", 12000, 500, 60000),
        danmakuCount: num(".s-dmnum", 200, 20, 2000),
        commentCount: num(".s-cmnum", 20, 1, 49),
        systemPrompt: $(".s-sys", r).value,
        extraPrompt: $(".s-extra", r).value,
      },
    };
  }

  async function saveSettings() {
    var r = $("#bpPanel .bpSet");
    var st = $(".bpStatus", r);
    var v = readSettings();
    try {
      await storageSet({ baseUrl: v.cfg.base, apiKey: v.cfg.key, model: v.cfg.model, opts: v.opts });
      var back = await storageGet(["baseUrl", "apiKey", "model", "opts"]);
      if (back.baseUrl !== v.cfg.base) throw new Error("回读不一致");
      applyCfg(v.cfg, v.opts);
      state.ctx = null;
      st.textContent = "已保存";
      st.style.color = "#2a9d5c";
      setTimeout(function () { st.textContent = ""; }, 1400);
    } catch (e) {
      st.textContent = "保存失败：" + (e && e.message || e);
      st.style.color = "#d94848";
    }
  }

  async function resetSettings() {
    var r = $("#bpPanel .bpSet");
    state.opts = Object.assign({}, DEFAULTS);
    fillSettings();
    await storageSet({ opts: state.opts });
    state.ctx = null;
    var st = $(".bpStatus", r);
    st.textContent = "已恢复默认";
    st.style.color = "#2a9d5c";
    setTimeout(function () { st.textContent = ""; }, 1400);
  }

  function applyCfg(cfg, opts) {
    state.cfg = cfg ? {
      base: normBase(cfg.base || ""), key: cfg.key || "", model: cfg.model || "",
      kind: kindOf(cfg.base || ""),
    } : { base: "", key: "", model: "", kind: "openai" };
    state.opts = Object.assign({}, DEFAULTS, opts || {});
  }

  function togglePanel(force) {
    state.open = typeof force === "boolean" ? force : !state.open;
    var p = $("#bpPanel");
    if (p) p.classList.toggle("open", state.open);
    var b = $('#bpRail .bpBtn[data-k="chat"]');
    if (b) b.classList.toggle("on", state.open);
    if (state.open) {
      toggleDl(false);
      refreshHead();
    } else {
      openSettings(false);
    }
  }

  /* ---------------- 下载 ---------------- */

  function buildDlPop() {
    var d = el("div");
    d.id = "bpDlPop";
    d.innerHTML =
      '<div class="bpPopTitle">下载视频</div>' +
      '<div class="bpDlOpt" data-wm="0">无水印</div>' +
      '<div class="bpDlOpt" data-wm="1">有水印</div>' +
      '<div class="bpBar bpHide"><i></i></div>' +
      '<div class="bpNote"></div>';
    document.documentElement.appendChild(d);
    d.addEventListener("click", function (e) {
      var o = e.target.closest("[data-wm]");
      if (o) startDl(o.getAttribute("data-wm") === "1" ? 1 : 0);
    });
    return d;
  }

  function toggleDl(force) {
    var open = typeof force === "boolean" ? force : !$("#bpDlPop").classList.contains("open");
    $("#bpDlPop").classList.toggle("open", open);
    var b = $('#bpRail .bpBtn[data-k="down"]');
    if (b) b.classList.toggle("on", open);
    if (open) {
      togglePanel(false);
      positionDl();
    }
  }

  function positionDl() {
    var pop = $("#bpDlPop");
    if (!pop) return;
    var h = pop.offsetHeight || 170;
    var top = window.innerHeight / 2 - h / 2;
    pop.style.top = Math.max(12, Math.min(window.innerHeight - h - 12, top)) + "px";
  }

  function startDl(wm) {
    var pop = $("#bpDlPop");
    var bar = $(".bpBar", pop), fill = $(".bpBar i", pop), note = $(".bpNote", pop);
    var btn = $('#bpRail .bpBtn[data-k="down"] .fill');
    if (state.dl) { clearInterval(state.dl); state.dl = null; }
    note.textContent = "准备中…";
    bar.classList.add("bpHide");
    var vd = window.__INITIAL_STATE__ && window.__INITIAL_STATE__.videoData;
    var aid = vd && vd.aid || "", cid = vd && vd.cid || "";
    var nm = encodeURIComponent((document.title || "").replace(/^【.*?】/, "")
      .replace(/_哔哩哔哩_bilibili$/, "").replace(/[\\/:*?"<>|]/g, "_").slice(0, 80) || "video");
    var done = false;
    var fail = function (t) {
      if (done) return;
      done = true;
      note.textContent = "失败：" + t;
      bar.classList.add("bpHide");
      if (btn) btn.style.height = "0";
    };
    var ctl = window.AbortController ? new AbortController() : null;
    var to = setTimeout(function () { if (ctl) ctl.abort(); fail("超时"); }, 15000);
    fetch("/__biliproxy/dl?wm=" + wm + "&name=" + nm + "&aid=" + aid + "&cid=" + cid,
          { cache: "no-store", signal: ctl ? ctl.signal : undefined })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        clearTimeout(to);
        if (done) return;
        if (!j.ok) { fail(j.msg || "未知错误"); return; }
        bar.classList.remove("bpHide");
        fill.style.width = "0%";
        note.textContent = "下载中 0%";
        state.dl = setInterval(function () {
          fetch("/__biliproxy/dlstatus?id=" + j.id, { cache: "no-store" })
            .then(function (r) { return r.json(); })
            .then(function (s) {
              if (s.state === 2) {
                clearInterval(state.dl); state.dl = null; done = true;
                fill.style.width = "100%";
                if (btn) { btn.style.height = "100%"; setTimeout(function () { btn.style.height = "0"; }, 1800); }
                note.textContent = "完成：" + String(s.out || "").split(/[\\/]/).pop();
              } else if (s.state === 3) {
                clearInterval(state.dl); state.dl = null;
                fail(s.note || "失败");
              } else {
                fill.style.width = (s.pct || 0) + "%";
                if (btn) btn.style.height = (s.pct || 0) + "%";
                note.textContent = "下载中 " + (s.pct || 0) + "%";
              }
            })
            .catch(function () {});
        }, 500);
      })
      .catch(function () { clearTimeout(to); fail("请求失败"); });
  }

  /* ---------------- 对话 ---------------- */

  function escapeHtml(s) {
    return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  function inlineMd(s) {
    s = escapeHtml(s);
    s = s.replace(/`([^`\n]+)`/g, "<code>$1</code>");
    s = s.replace(/!\[([^\]]*)\]\(([^)\s]+)\)/g, function (m, alt, url) {
      if (!/^(https?:|data:image\/)/i.test(url)) return m;
      return '<img alt="' + alt + '" src="' + url + '">';
    });
    s = s.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, function (m, txt, url) {
      if (!/^https?:/i.test(url)) return m;
      return '<a href="' + url + '" target="_blank" rel="noreferrer">' + txt + "</a>";
    });
    s = s.replace(/\*\*([^*\n]+)\*\*/g, "<strong>$1</strong>");
    s = s.replace(/__([^_\n]+)__/g, "<strong>$1</strong>");
    s = s.replace(/(^|[^*])\*([^*\n]+)\*/g, "$1<em>$2</em>");
    s = s.replace(/~~([^~\n]+)~~/g, "<del>$1</del>");
    return s;
  }

  // 行块级解析：代码块先摘出来，剩下的逐行收成段落/列表/引用/表格
  function md(text) {
    var src = String(text == null ? "" : text).replace(/\r\n?/g, "\n");
    var blocks = [];

    src = src.replace(/^[ \t]*```([^\n`]*)\n([\s\S]*?)^[ \t]*```[ \t]*$/gm,
      function (m, lang, code) {
        blocks.push({ lang: String(lang).trim(), code: code.replace(/\n$/, "") });
        return "\u0001" + (blocks.length - 1) + "\u0001";
      });

    var lines = src.split("\n");
    var out = [], para = [], quote = [], list = null, table = null;

    function flushPara() {
      if (!para.length) return;
      out.push("<p>" + para.map(inlineMd).join("<br>") + "</p>");
      para = [];
    }
    function flushList() {
      if (!list) return;
      out.push("<" + list.tag + ">" + list.items.map(function (t) {
        return "<li>" + inlineMd(t) + "</li>";
      }).join("") + "</" + list.tag + ">");
      list = null;
    }
    function flushQuote() {
      if (!quote.length) return;
      out.push("<blockquote>" + quote.map(inlineMd).join("<br>") + "</blockquote>");
      quote = [];
    }
    function flushTable() {
      if (!table) return;
      var h = "<table><thead><tr>";
      for (var i = 0; i < table.head.length; i++) h += "<th>" + inlineMd(table.head[i]) + "</th>";
      h += "</tr></thead><tbody>";
      for (var r = 0; r < table.rows.length; r++) {
        h += "<tr>";
        for (var c = 0; c < table.head.length; c++)
          h += "<td>" + inlineMd(table.rows[r][c] || "") + "</td>";
        h += "</tr>";
      }
      out.push(h + "</tbody></table>");
      table = null;
    }
    function flushAll() { flushPara(); flushList(); flushQuote(); flushTable(); }

    function splitRow(l) {
      var t = l.trim().replace(/^\|/, "").replace(/\|$/, "");
      return t.split("|").map(function (x) { return x.trim(); });
    }

    for (var i = 0; i < lines.length; i++) {
      var line = lines[i];

      var ph = line.match(/^\u0001(\d+)\u0001\s*$/);
      if (ph) {
        flushAll();
        var b = blocks[+ph[1]] || { lang: "", code: "" };
        out.push('<pre><code' + (b.lang ? ' class="lang-' + escapeHtml(b.lang) + '"' : "") +
                 ">" + escapeHtml(b.code) + "</code></pre>");
        continue;
      }

      if (!line.trim()) { flushAll(); continue; }

      var hm = line.match(/^(#{1,6})\s+(.*)$/);
      if (hm) {
        flushAll();
        var lv = Math.min(6, Math.max(3, hm[1].length + 2));
        out.push("<h" + lv + ">" + inlineMd(hm[2]) + "</h" + lv + ">");
        continue;
      }

      if (/^\s*([-*_])\s*\1\s*\1[\s\-*_]*$/.test(line)) { flushAll(); out.push("<hr>"); continue; }

      if (/^\s*>\s?/.test(line)) {
        flushPara(); flushList(); flushTable();
        quote.push(line.replace(/^\s*>\s?/, ""));
        continue;
      }

      // 表格：表头 + 分隔行
      if (line.indexOf("|") >= 0 && i + 1 < lines.length && /^\s*\|?[\s:|-]+\|[\s:|-]*$/.test(lines[i + 1])) {
        flushPara(); flushList(); flushQuote();
        table = { head: splitRow(line), rows: [] };
        i++;
        continue;
      }
      if (table && line.indexOf("|") >= 0) { table.rows.push(splitRow(line)); continue; }

      var ul = line.match(/^\s*[-*+]\s+(.*)$/);
      if (ul) {
        flushPara(); flushQuote(); flushTable();
        if (!list || list.tag !== "ul") { flushList(); list = { tag: "ul", items: [] }; }
        list.items.push(ul[1]);
        continue;
      }
      var ol = line.match(/^\s*\d+[.)]\s+(.*)$/);
      if (ol) {
        flushPara(); flushQuote(); flushTable();
        if (!list || list.tag !== "ol") { flushList(); list = { tag: "ol", items: [] }; }
        list.items.push(ol[1]);
        continue;
      }

      flushList(); flushTable();
      para.push(line);
    }
    flushAll();
    return out.join("");
  }

  function addMsg(role, text, isMd) {
    var chat = $("#bpPanel .bpChat");
    if (!chat) return null;
    var d = el("div", "bpMsg " + role);
    if (isMd && role === "ai") d.innerHTML = md(text);
    else d.textContent = text;
    chat.appendChild(d);
    chat.scrollTop = chat.scrollHeight;
    return d;
  }

  async function refreshHead() {
    var info = await getInfo();
    state.info = info;
    var t = $("#bpPanel .bpVtitle"), m = $("#bpPanel .bpVmeta");
    if (t) t.textContent = info.title || "(无标题)";
    if (m) {
      var bits = [];
      if (info.owner) bits.push(info.owner);
      if (info.bvid) bits.push(info.bvid);
      if (info.duration) bits.push(fmt(info.duration));
      m.textContent = bits.join(" · ");
    }
  }

  async function ensureCtx() {
    if (state.ctx) return state.ctx;
    var info = state.info || await getInfo();
    state.info = info;
    var o = state.opts;
    var tasks = [];
    if (o.subtitle) tasks.push("字幕");
    if (o.danmaku) tasks.push("弹幕");
    if (o.comments) tasks.push("评论");
    var framesOn = o.frameMode === "fixed" ? o.frameFixed > 0 : o.framePerMin > 0;
    addMsg("sys", "正在抓取：" + ((tasks.join("、") || "基础信息") + (framesOn ? "、画面帧" : "")) + "…");

    var safe = async function (label, fn) {
      try { return await fn(); }
      catch (e) { addMsg("sys", label + " 失败：" + (e && e.message || e)); return null; }
    };

    var subtitle = o.subtitle ? (await safe("字幕", function () { return getSubtitles(info); })) || "" : "";
    var danmaku = o.danmaku ? (await safe("弹幕", function () { return getDanmaku(info); })) || "" : "";
    var comments = o.comments ? (await safe("评论", function () { return getComments(info); })) || "" : "";
    var frames = [];
    if (framesOn && info.duration) frames = (await safe("画面帧", function () { return captureFrames(info); })) || [];

    var got = [];
    if (subtitle) got.push("字幕 " + subtitle.length + " 字");
    if (danmaku) got.push("弹幕 " + danmaku.split(" / ").length + " 条");
    if (comments) got.push("评论 " + comments.split("\n").length + " 条");
    if (frames.length) got.push(frames.length + " 帧画面");
    addMsg("sys", "已获取：" + (got.join("，") || "仅标题/简介"));

    state.ctx = { info: info, subtitle: subtitle, danmaku: danmaku, comments: comments, frames: frames };
    return state.ctx;
  }

  async function seedHistory() {
    var ctx = await ensureCtx();
    var prompt = buildPrompt(ctx);
    state.history = [
      { role: "system", content: state.opts.systemPrompt || DEFAULTS.systemPrompt },
      { role: "user", content: buildUserContent(prompt, ctx.frames) },
    ];
    return ctx;
  }

  async function summarize() {
    if (!state.cfg.base || !state.cfg.key || !state.cfg.model) {
      openSettings(true);
      addMsg("sys", "先填好接口再总结");
      return;
    }
    var btn = $('#bpPanel [data-act="sum"]');
    if (btn) { btn.disabled = true; btn.textContent = "处理中…"; }
    addMsg("user", "总结这个视频");
    try {
      await seedHistory();
      var reply = await ask(state.history);
      state.history.push({ role: "assistant", content: reply });
      addMsg("ai", reply, true);
    } catch (e) {
      addMsg("sys", "出错了：" + (e && e.message || e));
    } finally {
      if (btn) { btn.disabled = false; btn.textContent = "总结这个视频"; }
    }
  }

  async function send() {
    var inp = $("#bpPanel .bpInput");
    var text = inp.value.trim();
    if (!text) return;
    if (!state.cfg.base || !state.cfg.key || !state.cfg.model) {
      openSettings(true);
      addMsg("sys", "先填好接口再提问");
      return;
    }
    inp.value = "";
    addMsg("user", text);
    if (!state.history.length) {
      try { await seedHistory(); }
      catch (e) { addMsg("sys", "出错了：" + (e && e.message || e)); return; }
    }
    state.history.push({ role: "user", content: text });
    var btn = $('#bpPanel [data-act="send"]');
    if (btn) btn.disabled = true;
    var bub = addMsg("ai", "…");
    try {
      var reply = await ask(state.history);
      state.history.push({ role: "assistant", content: reply });
      bub.innerHTML = md(reply);
    } catch (e) {
      bub.textContent = "出错了：" + (e && e.message || e);
    } finally {
      if (btn) btn.disabled = false;
      var chat = $("#bpPanel .bpChat");
      if (chat) chat.scrollTop = chat.scrollHeight;
    }
  }

  /* ================= 启动 ================= */

  async function boot() {
    applyCfg(null, null);
    buildRail();
    buildPanel();
    buildDlPop();
    var s = await storageGet(["baseUrl", "apiKey", "model", "opts"]);
    applyCfg({ base: s.baseUrl || "", key: s.apiKey || "", model: s.model || "" }, s.opts);
    refreshHead();

    document.addEventListener("click", function (e) {
      if (!e.target.closest("#bpRail, #bpPanel, #bpDlPop")) {
        if ($("#bpDlPop").classList.contains("open")) toggleDl(false);
      }
    }, true);
    window.addEventListener("resize", function () {
      if ($("#bpDlPop").classList.contains("open")) positionDl();
    });
    window.addEventListener("popstate", function () {
      state.ctx = null; state.history = [];
      var c = $("#bpPanel .bpChat");
      if (c) c.innerHTML = "";
      refreshHead();
    });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
  else boot();
})();
