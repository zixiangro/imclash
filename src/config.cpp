#include "config.h"

#include "fsutil.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace imclash::config {
namespace {

const char* kBom = "\xEF\xBB\xBF";

std::string stripBom(const std::string& text) {
    if (text.size() >= 3 &&
        text.compare(0, 3, kBom) == 0) {
        return text.substr(3);
    }
    return text;
}

std::string stripInlineComment(const std::string& text) {
    bool single = false;
    bool doubleQuote = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\'' && !doubleQuote) {
            single = !single;
        } else if (ch == '"' && !single) {
            doubleQuote = !doubleQuote;
        } else if (ch == '#' && !single && !doubleQuote) {
            if (index == 0 || std::isspace(static_cast<unsigned char>(text[index - 1]))) {
                return text.substr(0, index);
            }
        }
    }
    return text;
}

std::string unquote(const std::string& value) {
    std::string text = util::trim(value);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return text.substr(1, text.size() - 2);
    }
    if (text.size() >= 2 && text.front() == '\'' && text.back() == '\'') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::string scalarAfterKey(const std::string& text, const std::string& key) {
    std::size_t index = 0;
    while (index < text.size() &&
           std::isspace(static_cast<unsigned char>(text[index]))) {
        ++index;
    }
    if (text.compare(index, key.size(), key) != 0) return {};
    index += key.size();
    while (index < text.size() &&
           std::isspace(static_cast<unsigned char>(text[index]))) {
        ++index;
    }
    if (index >= text.size() || text[index] != ':') return {};
    ++index;
    return unquote(stripInlineComment(text.substr(index)));
}

bool isTopLevelKey(const std::string& line, const std::string& key) {
    if (line.compare(0, key.size(), key) != 0) return false;
    if (line.size() == key.size()) return false;
    std::size_t index = key.size();
    while (index < line.size() &&
           std::isspace(static_cast<unsigned char>(line[index]))) {
        ++index;
    }
    return index < line.size() && line[index] == ':';
}

std::string topLevelString(const std::string& content, const std::string& key) {
    const std::vector<std::string> lines = util::splitLines(content);
    for (const std::string& line : lines) {
        if (line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '#') {
            continue;
        }
        if (isTopLevelKey(line, key)) {
            return scalarAfterKey(line, key);
        }
    }
    return {};
}

int topLevelInt(const std::string& content, const std::string& key) {
    const std::string value = topLevelString(content, key);
    if (value.empty()) return 0;
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || (end != nullptr && *end != '\0')) return 0;
    if (parsed < 0 || parsed > 65535) return 0;
    return static_cast<int>(parsed);
}

std::vector<std::string> proxyOrder(const std::string& content) {
    std::vector<std::string> names;
    bool inProxies = false;
    for (const std::string& rawLine : util::splitLines(content)) {
        const std::string line = stripBom(rawLine);
        if (!inProxies) {
            if (isTopLevelKey(line, "proxies")) inProxies = true;
            continue;
        }
        if (line.empty() || line[0] == '#') continue;
        if (line[0] != ' ' && line[0] != '\t') break;
        std::string rest = util::trim(line);
        if (rest.size() < 2 || rest[0] != '-') continue;
        rest = util::trim(rest.substr(1));
        std::string name;
        if (util::startsWith(rest, "name:")) {
            name = unquote(stripInlineComment(rest.substr(5)));
        } else if (!rest.empty() && rest[0] == '{') {
            const std::size_t key = rest.find("name:");
            if (key != std::string::npos) {
                name = unquote(stripInlineComment(rest.substr(key + 5)));
                const std::size_t comma = name.find(',');
                if (comma != std::string::npos) name = util::trim(name.substr(0, comma));
            }
        }
        if (!name.empty()) names.push_back(name);
    }
    return names;
}

bool validApiPort(int port) {
    return port > 0 && port <= 65535;
}

// Existing subscriptions often carry their source URL in a banner comment
// instead of a first-line # url:. Recognise only clearly labelled comments so
// a ruleset or homepage URL elsewhere in the file is not mistaken for the
// subscription source.
std::string labeledCommentUrl(const std::string& content) {
    const std::vector<std::string> lines = util::splitLines(stripBom(content));
    const std::size_t limit = std::min<std::size_t>(lines.size(), 12);
    for (std::size_t index = 0; index < limit; ++index) {
        const std::string line = util::trim(lines[index]);
        if (line.empty() || line[0] != '#') continue;
        const std::string lower = util::toLower(line);
        const bool labeled = lower.find("url") != std::string::npos ||
                             lower.find("link") != std::string::npos ||
                             lower.find("subscription") != std::string::npos ||
                             line.find("\xE9\x93\xBE\xE6\x8E\xA5") != std::string::npos ||
                             line.find("\xE8\xAE\xA2\xE9\x98\x85") != std::string::npos;
        if (!labeled) continue;
        std::size_t start = line.find("https://");
        if (start == std::string::npos) start = line.find("http://");
        if (start == std::string::npos) continue;
        std::size_t end = start;
        while (end < line.size() &&
               !std::isspace(static_cast<unsigned char>(line[end])) &&
               line[end] != '"' && line[end] != '\'' && line[end] != '<' &&
               line[end] != '>' && line[end] != ')' && line[end] != ']') {
            ++end;
        }
        std::string url = line.substr(start, end - start);
        while (!url.empty() && (url.back() == '.' || url.back() == ',' ||
                                url.back() == ';')) {
            url.pop_back();
        }
        if (!url.empty()) return url;
    }
    return {};
}

} // namespace

Info parse(const std::string& rawContent) {
    Info info;
    info.content = stripBom(rawContent);
    info.present = true;
    info.sourceUrl = updateUrlFromContent(info.content);
    if (info.sourceUrl.empty()) info.sourceUrl = labeledCommentUrl(info.content);
    info.mixedPort = topLevelInt(info.content, "mixed-port");
    info.httpPort = topLevelInt(info.content, "port");
    info.socksPort = topLevelInt(info.content, "socks-port");
    info.externalController = topLevelString(info.content, "external-controller");
    info.secret = topLevelString(info.content, "secret");
    info.validClashYaml = looksLikeClashYaml(info.content);
    info.proxyOrder = proxyOrder(info.content);

    std::string controller = info.externalController;
    if (controller.empty()) {
        info.apiHost = "127.0.0.1";
        info.apiPort = 9090;
        return info;
    }
    if (util::startsWith(controller, "http://")) controller = controller.substr(7);
    if (util::startsWith(controller, "https://")) controller = controller.substr(8);
    while (!controller.empty() && (controller.back() == '/' || controller.back() == ' ')) {
        controller.pop_back();
    }

    std::string host;
    int port = 9090;
    if (!controller.empty() && controller.front() == '[') {
        const std::size_t close = controller.find(']');
        if (close != std::string::npos) {
            host = controller.substr(1, close - 1);
            if (close + 1 < controller.size() && controller[close + 1] == ':') {
                port = std::atoi(controller.c_str() + close + 2);
            }
        }
    } else {
        const std::size_t colon = controller.rfind(':');
        if (colon == std::string::npos) {
            host = controller;
            port = 9090;
        } else {
            host = controller.substr(0, colon);
            port = std::atoi(controller.c_str() + colon + 1);
        }
    }
    if (host.empty() || host == "0.0.0.0" || host == "::" || host == "localhost") {
        host = "127.0.0.1";
    }
    if (!validApiPort(port)) port = 9090;
    info.apiHost = host;
    info.apiPort = port;
    return info;
}

Info read(const std::string& path) {
    Info info;
    std::string content;
    std::string error;
    if (!fs::readText(path, content, error)) {
        info.present = false;
        return info;
    }
    info = parse(content);
    info.present = true;
    return info;
}

std::string updateUrlFromContent(const std::string& content) {
    const std::string clean = stripBom(content);
    const std::size_t newline = clean.find('\n');
    std::string line = clean.substr(0, newline == std::string::npos ? clean.size() : newline);
    line = util::trim(line);
    if (line.empty() || line[0] != '#') return {};
    line = util::trim(line.substr(1));
    const std::string lower = util::toLower(line);
    if (util::startsWith(lower, "url:")) {
        line = util::trim(line.substr(4));
    }
    if (!util::startsWith(line, "http://") && !util::startsWith(line, "https://")) {
        return {};
    }
    return line;
}

std::string withUpdateUrl(const std::string& rawContent, const std::string& url) {
    std::string content = stripBom(rawContent);
    const std::size_t newline = content.find('\n');
    if (newline != std::string::npos) {
        const std::string first = util::trim(content.substr(0, newline));
        const std::string lower = util::toLower(first);
        if (!first.empty() && first[0] == '#' &&
            (util::startsWith(lower, "# url:") ||
             util::startsWith(lower, "# http://") ||
             util::startsWith(lower, "# https://"))) {
            content = content.substr(newline + 1);
        }
    } else {
        const std::string first = util::trim(content);
        const std::string lower = util::toLower(first);
        if (!first.empty() && first[0] == '#' &&
            (util::startsWith(lower, "# url:") ||
             util::startsWith(lower, "# http://") ||
             util::startsWith(lower, "# https://"))) {
            content.clear();
        }
    }

    std::string cleanUrl = util::trim(url);
    cleanUrl.erase(std::remove_if(cleanUrl.begin(), cleanUrl.end(),
                                  [](unsigned char ch) { return ch < 0x20 || ch == 0x7F; }),
                   cleanUrl.end());
    if (util::startsWith(cleanUrl, "http://") || util::startsWith(cleanUrl, "https://")) {
        content = "# url: " + cleanUrl + "\n" + content;
    }
    if (!content.empty() && content.back() != '\n') content.push_back('\n');
    return content;
}

bool looksLikeBase64List(const std::string& rawContent) {
    std::string text;
    for (const unsigned char ch : stripBom(rawContent)) {
        if (!std::isspace(ch)) text.push_back(static_cast<char>(ch));
    }
    if (text.size() < 64) return false;
    for (const unsigned char ch : text) {
        const bool valid = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                           (ch >= '0' && ch <= '9') || ch == '+' || ch == '/' ||
                           ch == '=' || ch == '-' || ch == '_';
        if (!valid) return false;
    }
    return true;
}

bool looksLikeClashYaml(const std::string& rawContent) {
    const std::string content = util::trim(stripBom(rawContent));
    if (content.empty()) return false;
    if (content.front() == '<') return false;
    if (looksLikeBase64List(content)) return false;

    static const char* kKeys[] = {
        "proxies", "proxy-groups", "proxy-providers", "rules", "mixed-port",
        "port", "socks-port", "mode", "log-level", "external-controller",
        "dns", "tun", "listeners", "interface-name",
    };
    for (const std::string& line : util::splitLines(content)) {
        if (line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '#') {
            continue;
        }
        for (const char* key : kKeys) {
            if (isTopLevelKey(line, key)) return true;
        }
    }
    return false;
}

std::vector<std::string> parseProxyOrder(const std::string& content) {
    return proxyOrder(stripBom(content));
}

std::string proxyAddress(const Info& info) {
    const int port = info.mixedPort > 0 ? info.mixedPort : info.httpPort;
    if (port <= 0) return {};
    return "127.0.0.1:" + std::to_string(port);
}

std::string apiBaseUrl(const Info& info) {
    const std::string host = info.apiHost.find(':') != std::string::npos
        ? "[" + info.apiHost + "]"
        : info.apiHost;
    return "http://" + host + ":" + std::to_string(info.apiPort);
}

} // namespace imclash::config
