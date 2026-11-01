#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace imclash::util {

std::string trim(const std::string& text);
std::string toLower(std::string text);
bool startsWith(const std::string& text, const std::string& prefix);
bool endsWith(const std::string& text, const std::string& suffix);
std::vector<std::string> splitLines(const std::string& text);

// Percent-encodes one URL path segment (RFC 3986 unreserved characters stay).
std::string percentEncode(const std::string& text);

// Human-readable byte count, e.g. "128 KB".
std::string formatBytes(std::uint64_t bytes);

// "just now", "3 min ago", "2 h ago", "4 days ago", then a local date.
std::string formatRelativeTime(std::int64_t unixSeconds);

// "v1.19.21" and "1.19.21" compare by their numeric components.
int compareVersions(const std::string& left, const std::string& right);

// Extracts the first "v<digits>[.<digits>...]" token from text.
std::string findVersion(const std::string& text);

} // namespace imclash::util
