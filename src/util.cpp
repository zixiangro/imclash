#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

namespace imclash::util {
namespace {

bool isUnreserved(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

std::vector<int> versionParts(const std::string& version) {
    std::vector<int> parts;
    int current = -1;
    for (const unsigned char ch : version) {
        if (ch >= '0' && ch <= '9') {
            if (current < 0) current = 0;
            current = current * 10 + static_cast<int>(ch - '0');
        } else if (ch == '.') {
            if (current >= 0) {
                parts.push_back(current);
                current = -1;
            }
        } else if (current >= 0) {
            break;
        }
    }
    if (current >= 0) parts.push_back(current);
    return parts;
}

} // namespace

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    for (const char ch : text) {
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
            line.clear();
        } else {
            line.push_back(ch);
        }
    }
    if (!line.empty()) {
        if (line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::string percentEncode(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    const char* hex = "0123456789ABCDEF";
    for (const unsigned char ch : text) {
        if (isUnreserved(ch)) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 0x0F]);
        }
    }
    return out;
}

std::string formatBytes(std::uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    const char* units[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    if (value >= 100.0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f %s", value, units[unit]);
    } else if (value >= 10.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, units[unit]);
    }
    return buffer;
}

std::string formatRelativeTime(std::int64_t unixSeconds) {
    if (unixSeconds <= 0) return "never";
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    std::int64_t delta = now - unixSeconds;
    if (delta < 0) delta = 0;
    if (delta < 60) return "just now";
    if (delta < 3600) return std::to_string(delta / 60) + " min ago";
    if (delta < 86400) return std::to_string(delta / 3600) + " h ago";
    if (delta < 86400 * 7) return std::to_string(delta / 86400) + " days ago";

    std::time_t time = static_cast<std::time_t>(unixSeconds);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local);
    return buffer;
}

int compareVersions(const std::string& left, const std::string& right) {
    std::vector<int> a = versionParts(left);
    std::vector<int> b = versionParts(right);
    const std::size_t count = std::max(a.size(), b.size());
    for (std::size_t index = 0; index < count; ++index) {
        const int av = index < a.size() ? a[index] : 0;
        const int bv = index < b.size() ? b[index] : 0;
        if (av != bv) return av < bv ? -1 : 1;
    }
    return 0;
}

std::string findVersion(const std::string& text) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != 'v' && text[index] != 'V') continue;
        if (index + 1 >= text.size() ||
            !std::isdigit(static_cast<unsigned char>(text[index + 1]))) {
            continue;
        }
        std::size_t end = index + 1;
        while (end < text.size()) {
            const unsigned char ch = static_cast<unsigned char>(text[end]);
            if (std::isdigit(ch) || ch == '.') {
                ++end;
            } else {
                break;
            }
        }
        while (end > index + 1 && text[end - 1] == '.') --end;
        return text.substr(index, end - index);
    }
    return {};
}

} // namespace imclash::util
