#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace imclash::core_update {

struct CheckResult {
    bool ok = false;
    std::string current;
    std::string latest;
    std::string assetName;
    std::string downloadUrl;
    bool updateAvailable = false;
    std::string error;
};

CheckResult check(const std::string& currentVersion, const std::string& proxy);

struct DownloadResult {
    bool ok = false;
    std::string executablePath;
    std::string version;
    std::string tempDirectory;
    std::string error;
};

DownloadResult download(const std::string& url, const std::string& version,
                        const std::string& proxy,
                        const std::function<void(std::uint64_t, std::uint64_t)>& progress);

bool install(const std::string& newExecutable, std::string& error);

} // namespace imclash::core_update
