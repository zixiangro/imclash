#include "config.h"
#include "util.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

} // namespace

int main() {
    const std::string yaml =
        "mixed-port: 7890\n"
        "external-controller: 127.0.0.1:9091\n"
        "secret: \"hello:world\"\n"
        "proxies:\n"
        "  - name: \"HK 01\"\n"
        "    type: ss\n"
        "  - {name: JP-02, type: vmess}\n"
        "proxy-groups:\n"
        "  - name: PROXY\n";

    imclash::config::Info info = imclash::config::parse(yaml);
    expect(info.validClashYaml, "YAML detected");
    expect(info.mixedPort == 7890, "mixed-port parsed");
    expect(info.apiPort == 9091, "external controller port parsed");
    expect(info.secret == "hello:world", "secret parsed");
    expect(info.proxyOrder.size() == 2, "proxy order count");
    expect(info.proxyOrder[0] == "HK 01", "quoted proxy name");
    expect(info.proxyOrder[1] == "JP-02", "inline proxy name");
    expect(imclash::config::proxyAddress(info) == "127.0.0.1:7890",
           "proxy address");

    const std::string withUrl = imclash::config::withUpdateUrl(yaml, "https://example.com/sub");
    expect(imclash::config::updateUrlFromContent(withUrl) ==
               "https://example.com/sub",
           "update URL roundtrip");
    expect(withUrl.find("# url: https://example.com/sub\n") == 0,
           "URL is the first line");

    const std::string replaced =
        imclash::config::withUpdateUrl(withUrl, "https://example.com/other");
    expect(replaced.find("https://example.com/sub") == std::string::npos,
           "old URL removed");
    expect(imclash::config::updateUrlFromContent(replaced) ==
               "https://example.com/other",
           "URL replaced");

    const std::string bare = "# https://example.com/bare\nproxies: []\n";
    expect(imclash::config::updateUrlFromContent(bare) ==
               "https://example.com/bare",
           "bare URL comment accepted");

    const std::string legacy =
        "# banner\n"
        "## 链接：https://example.com/sub?token=x\n"
        "proxies: []\n";
    const imclash::config::Info legacyInfo = imclash::config::parse(legacy);
    expect(legacyInfo.sourceUrl == "https://example.com/sub?token=x",
           "labelled banner URL detected");

    expect(!imclash::config::looksLikeClashYaml("<html>bad</html>"),
           "HTML rejected");
    expect(!imclash::config::looksLikeClashYaml(
               "c3M6Ly9ZV1Z6TFRJMU5pMW5ZMjA2"),
           "short base64 rejected");
    const std::string base64(128, 'A');
    expect(imclash::config::looksLikeBase64List(base64), "base64 detected");
    expect(!imclash::config::looksLikeClashYaml(base64), "base64 config rejected");
    expect(imclash::config::looksLikeClashYaml("proxies:\n  - name: a\n"),
           "minimal proxies config accepted");

    expect(imclash::util::compareVersions("v1.19.21", "1.19.20") > 0,
           "version compare newer");
    expect(imclash::util::compareVersions("v1.19.21", "v1.19.21") == 0,
           "version compare equal");
    expect(imclash::util::findVersion("Mihomo Meta v1.19.21 windows/amd64") ==
               "v1.19.21",
           "version extraction");
    expect(imclash::util::percentEncode("HK 01/节点") ==
               "HK%2001%2F%E8%8A%82%E7%82%B9",
           "percent encoding");
    expect(imclash::util::formatBytes(128u * 1024u) == "128 KB",
           "byte formatting");
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    expect(imclash::util::formatRelativeTime(0) == "never",
           "zero time formatting");
    expect(imclash::util::formatRelativeTime(now) == "just now",
           "current time formatting");
    expect(imclash::util::formatRelativeTime(now - 120) == "2 min ago",
           "minute formatting");

    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::printf("all logic tests passed\n");
    return 0;
}
