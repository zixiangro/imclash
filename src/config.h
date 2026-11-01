#pragma once

#include <string>
#include <vector>

namespace imclash::config {

struct Info {
    bool present = false;
    std::string sourceUrl;
    int mixedPort = 0;
    int httpPort = 0;
    std::string externalController;
    std::string apiHost = "127.0.0.1";
    int apiPort = 9090;
    std::string secret;
    std::vector<std::string> proxyOrder;
    bool validClashYaml = false;
};

Info read(const std::string& path);
Info parse(const std::string& content);

// The first line is the only place the update URL lives:
//   # url: https://example.com/sub
// A bare "# https://..." first line is accepted when reading.
std::string updateUrlFromContent(const std::string& content);
std::string withUpdateUrl(const std::string& content, const std::string& url);

bool looksLikeClashYaml(const std::string& content);
bool looksLikeBase64List(const std::string& content);

std::string proxyAddress(const Info& info);
std::string apiBaseUrl(const Info& info);

} // namespace imclash::config
