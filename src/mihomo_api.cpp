#include "mihomo_api.h"

#include "http.h"
#include "util.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include <yyjson.h>

namespace imclash::mihomo_api {
namespace {

http::Options apiOptions(const config::Info& info, int timeoutMs = 5000) {
    http::Options options;
    options.userAgent = "imclash/0.1";
    options.accept = "application/json";
    options.timeoutMs = timeoutMs;
    options.maxBytes = 8u * 1024u * 1024u;
    if (!info.secret.empty()) options.authorization = "Bearer " + info.secret;
    return options;
}

bool isGroupType(const std::string& type) {
    static const char* kGroups[] = {
        "selector", "urltest", "fallback", "loadbalance", "relay",
        "direct", "reject", "compatible", "pass", "block", "dns",
    };
    const std::string lower = util::toLower(type);
    for (const char* group : kGroups) {
        if (lower == group) return true;
    }
    // mihomo also reports rule targets such as PassRule and RejectDrop.
    return lower.find("pass") != std::string::npos ||
           lower.find("reject") != std::string::npos;
}

bool isProxyGroupType(const std::string& type) {
    static const char* kGroups[] = {
        "selector", "urltest", "fallback", "loadbalance", "relay",
    };
    const std::string lower = util::toLower(type);
    for (const char* group : kGroups) {
        if (lower == group) return true;
    }
    return false;
}

bool isBuiltinName(const std::string& name) {
    static const char* kNames[] = {
        "DIRECT", "REJECT", "GLOBAL", "PASS", "COMPATIBLE",
    };
    for (const char* builtin : kNames) {
        if (name == builtin) return true;
    }
    return false;
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const unsigned char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

int readDelayFromHistory(yyjson_val* value) {
    yyjson_val* history = yyjson_obj_get(value, "history");
    if (history == nullptr || !yyjson_is_arr(history)) return -1;
    const std::size_t count = yyjson_arr_size(history);
    if (count == 0) return -1;
    yyjson_val* last = yyjson_arr_get(history, count - 1);
    if (last == nullptr || !yyjson_is_obj(last)) return -1;
    yyjson_val* delay = yyjson_obj_get(last, "delay");
    if (delay == nullptr || !yyjson_is_int(delay)) return -1;
    const int valueDelay = static_cast<int>(yyjson_get_sint(delay));
    return valueDelay > 0 ? valueDelay : -1;
}

} // namespace

bool ping(const config::Info& info, std::string& version, std::string& error) {
    version.clear();
    error.clear();
    const http::Response response = http::get(
        config::apiBaseUrl(info) + "/version", apiOptions(info, 3000));
    if (!response.ok) {
        error = response.error.empty() ? "mihomo API is not reachable." : response.error;
        return false;
    }
    yyjson_doc* document = yyjson_read(response.body.data(), response.body.size(), 0);
    if (document == nullptr) {
        error = "Invalid /version response.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(document);
    yyjson_val* value = root != nullptr ? yyjson_obj_get(root, "version") : nullptr;
    if (value != nullptr && yyjson_is_str(value)) {
        version.assign(yyjson_get_str(value), yyjson_get_len(value));
    }
    yyjson_doc_free(document);
    if (version.empty()) {
        error = "mihomo returned no version.";
        return false;
    }
    return true;
}

bool fetchState(const config::Info& info, std::vector<Node>& nodes,
                std::vector<Group>& groups, std::string& error) {
    nodes.clear();
    groups.clear();
    error.clear();
    const http::Response response = http::get(
        config::apiBaseUrl(info) + "/proxies", apiOptions(info, 8000));
    if (!response.ok) {
        error = response.error.empty() ? "Cannot read proxies." : response.error;
        return false;
    }
    yyjson_doc* document = yyjson_read(response.body.data(), response.body.size(), 0);
    if (document == nullptr) {
        error = "Invalid /proxies response.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(document);
    yyjson_val* proxies = root != nullptr ? yyjson_obj_get(root, "proxies") : nullptr;
    if (proxies == nullptr || !yyjson_is_obj(proxies)) {
        error = "mihomo returned no proxy list.";
        yyjson_doc_free(document);
        return false;
    }

    yyjson_obj_iter iterator = yyjson_obj_iter_with(proxies);
    yyjson_val* key = nullptr;
    while ((key = yyjson_obj_iter_next(&iterator)) != nullptr) {
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (value == nullptr || !yyjson_is_obj(value)) continue;
        std::string name(yyjson_get_str(key), yyjson_get_len(key));
        if (name.empty()) continue;

        yyjson_val* typeValue = yyjson_obj_get(value, "type");
        std::string type;
        if (typeValue != nullptr && yyjson_is_str(typeValue)) {
            type.assign(yyjson_get_str(typeValue), yyjson_get_len(typeValue));
        }

        if (isProxyGroupType(type)) {
            Group group;
            group.name = std::move(name);
            group.type = std::move(type);
            yyjson_val* nowValue = yyjson_obj_get(value, "now");
            if (nowValue != nullptr && yyjson_is_str(nowValue)) {
                group.now.assign(yyjson_get_str(nowValue), yyjson_get_len(nowValue));
            }
            yyjson_val* allValue = yyjson_obj_get(value, "all");
            if (allValue != nullptr && yyjson_is_arr(allValue)) {
                const std::size_t count = yyjson_arr_size(allValue);
                for (std::size_t index = 0; index < count; ++index) {
                    yyjson_val* item = yyjson_arr_get(allValue, index);
                    if (item != nullptr && yyjson_is_str(item)) {
                        group.all.emplace_back(yyjson_get_str(item),
                                               yyjson_get_len(item));
                    }
                }
            }
            groups.push_back(std::move(group));
            continue;
        }

        if (isBuiltinName(name) || isGroupType(type)) continue;
        Node node;
        node.name = std::move(name);
        node.type = std::move(type);
        node.delay = readDelayFromHistory(value);
        nodes.push_back(std::move(node));
    }
    yyjson_doc_free(document);
    return true;
}

bool fetchProxies(const config::Info& info, std::vector<Node>& nodes,
                  std::string& error) {
    std::vector<Group> groups;
    return fetchState(info, nodes, groups, error);
}

bool selectNode(const config::Info& info, const std::string& group,
                const std::string& node, std::string& error) {
    error.clear();
    const std::string url = config::apiBaseUrl(info) + "/proxies/" +
                            util::percentEncode(group);
    const std::string body = "{\"name\":\"" + jsonEscape(node) + "\"}";
    const http::Response response = http::putJson(url, body, apiOptions(info, 8000));
    if (!response.ok) {
        error = response.error.empty() ? "mihomo rejected the selection."
                                       : response.error;
        return false;
    }
    return true;
}

bool testDelay(const config::Info& info, const std::string& name,
               int timeoutMs, const std::string& testUrl, int& delay,
               std::string& error) {
    delay = -1;
    error.clear();
    const std::string encodedName = util::percentEncode(name);
    const std::string encodedUrl = util::percentEncode(testUrl);
    const std::string url = config::apiBaseUrl(info) + "/proxies/" + encodedName +
                            "/delay?timeout=" + std::to_string(timeoutMs) +
                            "&url=" + encodedUrl;
    const http::Response response = http::get(url, apiOptions(info, timeoutMs + 3000));
    if (!response.ok) {
        error = response.error.empty() ? "Delay test failed." : response.error;
        return false;
    }
    yyjson_doc* document = yyjson_read(response.body.data(), response.body.size(), 0);
    if (document == nullptr) {
        error = "Invalid delay response.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(document);
    yyjson_val* value = root != nullptr ? yyjson_obj_get(root, "delay") : nullptr;
    if (value == nullptr || !yyjson_is_int(value)) {
        error = "No delay in response.";
        yyjson_doc_free(document);
        return false;
    }
    delay = static_cast<int>(yyjson_get_sint(value));
    yyjson_doc_free(document);
    if (delay <= 0) {
        delay = -1;
        error = "Timeout.";
        return false;
    }
    return true;
}

bool reload(const config::Info& info, const std::string& configPath,
            std::string& error) {
    error.clear();
    const std::string body = "{\"path\":\"" + jsonEscape(configPath) + "\"}";
    const http::Response response = http::putJson(
        config::apiBaseUrl(info) + "/configs?force=true", body,
        apiOptions(info, 15000));
    if (!response.ok) {
        error = response.error.empty() ? "mihomo rejected the configuration." : response.error;
        return false;
    }
    return true;
}

} // namespace imclash::mihomo_api
