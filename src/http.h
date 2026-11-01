#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace imclash::http {

struct Options {
    std::string userAgent = "imclash/0.1";
    // Empty means a direct connection. Otherwise "host:port".
    std::string proxy;
    std::string authorization;
    std::string accept = "*/*";
    int timeoutMs = 30000;
    std::size_t maxBytes = 16u * 1024u * 1024u;
    std::function<void(std::uint64_t, std::uint64_t)> progress;
};

struct Response {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string error;
};

Response get(const std::string& url, const Options& options = {});
Response putJson(const std::string& url, const std::string& json,
                 const Options& options = {});
bool downloadToFile(const std::string& url, const std::string& path,
                    const Options& options, std::string& error);

} // namespace imclash::http
