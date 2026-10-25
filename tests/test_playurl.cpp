// 纯逻辑测试：playurl 过滤 + fnval 去 AV1 位
#include "../src/common.h"
#include "../src/playurl.h"

static int fails = 0;
static void check(bool ok, const char* what) {
    if (!ok) { printf("FAIL: %s\n", what); ++fails; }
    else printf("ok  : %s\n", what);
}

int main() {
    // 1) 有 avc1 时，选 H.264 应只留 codecid=7
    std::string j1 =
        "{\"code\":0,\"data\":{\"dash\":{\"duration\":100,\"video\":["
        "{\"id\":80,\"codecid\":7,\"codecs\":\"avc1\",\"width\":1920,\"height\":1080},"
        "{\"id\":80,\"codecid\":12,\"codecs\":\"hev1\",\"width\":1920,\"height\":1080},"
        "{\"id\":80,\"codecid\":13,\"codecs\":\"av01\",\"width\":1920,\"height\":1080}"
        "]}}}";
    std::string r1 = filterPlayurlJson(j1, 7);
    check(r1.find("\"codecid\":12") == std::string::npos, "drop hevc");
    check(r1.find("\"codecid\":13") == std::string::npos, "drop av1");
    check(r1.find("\"codecid\":7") != std::string::npos, "keep avc");

    // 2) 只有 AV1（高清独占）时返回空 -> 触发降清晰度兜底
    std::string j2 =
        "{\"data\":{\"dash\":{\"video\":["
        "{\"id\":80,\"codecid\":13,\"codecs\":\"av01\"}"
        "]}}}";
    check(filterPlayurlJson(j2, 7).empty(), "av1-only -> empty (fallback)");

    // 3) 选 AV1 时保留 13
    std::string r3 = filterPlayurlJson(j1, 13);
    check(r3.find("\"codecid\":7") == std::string::npos, "av1 target drops avc");
    check(r3.find("\"codecid\":13") != std::string::npos, "av1 target keeps av1");

    // 4) fnval 去掉 AV1 位（2048）
    check(stripAv1Bit("/x/player/wbi/playurl?avid=1&fnval=4048&fnver=0").find("fnval=2000&") != std::string::npos,
          "fnval 4048 -> 2000");
    check(stripAv1Bit("/x/player/wbi/playurl?avid=1&fnval=16") ==
          "/x/player/wbi/playurl?avid=1&fnval=16", "fnval without av1 untouched");
    check(stripAv1Bit("/x/player/wbi/playurl?avid=1&fnval=2064&qn=80").find("fnval=16&qn=80") != std::string::npos,
          "fnval 2064 -> 16 keeps qn");
    check(stripAv1Bit("/x/player/wbi/playurl?avid=1").find("fnval") == std::string::npos,
          "no fnval untouched");

    // 5) 无匹配编码时原样放行（空串 = 不改写）
    std::string j5 = "{\"data\":{\"dash\":{\"video\":[{\"id\":80,\"codecid\":12}]}}}";
    check(filterPlayurlJson(j5, 7).empty(), "hevc-only + want avc -> empty");

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
