#include "http.h"

#include "fsutil.h"
#include "utf8.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")
#endif

namespace imclash::http {
namespace {

#if defined(_WIN32)

struct Handle {
    HINTERNET value = nullptr;
    ~Handle() {
        if (value != nullptr) WinHttpCloseHandle(value);
    }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::string errorText(const char* prefix) {
    const DWORD code = GetLastError();
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s (error %lu)", prefix,
                  static_cast<unsigned long>(code));
    return buffer;
}

struct CrackedUrl {
    std::wstring host;
    std::wstring path;
    int port = 0;
    bool secure = false;
};

bool crackUrl(const std::string& url, CrackedUrl& out) {
    const std::wstring wide = imclash::utf8::widen(url);
    if (wide.empty()) return false;

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &components)) return false;

    out.host.assign(components.lpszHostName, components.dwHostNameLength);
    if (components.dwUrlPathLength > 0) {
        out.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    }
    if (components.dwExtraInfoLength > 0) {
        out.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (out.path.empty()) out.path = L"/";
    out.port = static_cast<int>(components.nPort);
    out.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return !out.host.empty() && out.port > 0;
}

struct PreparedRequest {
    Handle session;
    Handle connection;
    Handle request;
    CrackedUrl parsed;
    DWORD contentLength = 0;
    std::string error;
};

bool prepare(const std::string& url, const std::string& method,
             const std::string& body, bool json, const Options& options,
             PreparedRequest& prepared) {
    if (!crackUrl(url, prepared.parsed)) {
        prepared.error = "Invalid URL.";
        return false;
    }

    const std::string agent = options.userAgent.empty() ? "imclash/0.1" : options.userAgent;
    const std::wstring wideAgent = imclash::utf8::widen(agent);
    const std::wstring wideProxy = imclash::utf8::widen(options.proxy);
    const bool useProxy = !options.proxy.empty();
    prepared.session.value = WinHttpOpen(
        wideAgent.c_str(),
        useProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
        useProxy ? wideProxy.c_str() : WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (prepared.session.value == nullptr) {
        prepared.error = errorText("WinHttpOpen failed");
        return false;
    }

    const int timeout = options.timeoutMs > 0 ? options.timeoutMs : 30000;
    WinHttpSetTimeouts(prepared.session.value, timeout, timeout, timeout, timeout);

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(prepared.session.value, WINHTTP_OPTION_REDIRECT_POLICY,
                     &redirect, sizeof(redirect));

    prepared.connection.value = WinHttpConnect(
        prepared.session.value, prepared.parsed.host.c_str(),
        static_cast<INTERNET_PORT>(prepared.parsed.port), 0);
    if (prepared.connection.value == nullptr) {
        prepared.error = errorText("WinHttpConnect failed");
        return false;
    }

    const std::wstring wideMethod = imclash::utf8::widen(method);
    prepared.request.value = WinHttpOpenRequest(
        prepared.connection.value, wideMethod.c_str(), prepared.parsed.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        prepared.parsed.secure ? WINHTTP_FLAG_SECURE : 0);
    if (prepared.request.value == nullptr) {
        prepared.error = errorText("WinHttpOpenRequest failed");
        return false;
    }

    std::wstring headers = L"User-Agent: ";
    headers += wideAgent;
    headers += L"\r\nAccept: ";
    headers += imclash::utf8::widen(options.accept.empty() ? "*/*" : options.accept);
    headers += L"\r\nAccept-Encoding: identity\r\n";
    if (!options.authorization.empty()) {
        headers += L"Authorization: ";
        headers += imclash::utf8::widen(options.authorization);
        headers += L"\r\n";
    }
    if (json) {
        headers += L"Content-Type: application/json\r\n";
    }

    const DWORD bodySize = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(
            prepared.request.value, headers.c_str(), static_cast<DWORD>(-1),
            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
            bodySize, bodySize, 0)) {
        prepared.error = errorText("WinHttpSendRequest failed");
        return false;
    }
    if (!WinHttpReceiveResponse(prepared.request.value, nullptr)) {
        prepared.error = errorText("WinHttpReceiveResponse failed");
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(prepared.request.value,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        prepared.error = errorText("Cannot read HTTP status");
        return false;
    }
    DWORD length = 0;
    DWORD lengthSize = sizeof(length);
    if (WinHttpQueryHeaders(prepared.request.value,
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &length, &lengthSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        prepared.contentLength = length;
    }
    return true;
}

Response finish(PreparedRequest& prepared, const Options& options) {
    Response response;

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(prepared.request.value,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    response.status = static_cast<int>(status);

    wchar_t contentType[256]{};
    DWORD contentTypeSize = sizeof(contentType);
    if (WinHttpQueryHeaders(prepared.request.value, WINHTTP_QUERY_CONTENT_TYPE,
                            WINHTTP_HEADER_NAME_BY_INDEX, contentType,
                            &contentTypeSize, WINHTTP_NO_HEADER_INDEX)) {
        response.contentType = imclash::utf8::narrow(contentType);
    }

    DWORD length = 0;
    DWORD lengthSize = sizeof(length);
    if (WinHttpQueryHeaders(prepared.request.value,
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &length, &lengthSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        prepared.contentLength = length;
    } else {
        prepared.contentLength = 0;
    }
    if (prepared.contentLength > options.maxBytes) {
        response.error = "Response is too large.";
        return response;
    }

    std::uint64_t total = prepared.contentLength;
    std::uint64_t received = 0;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(prepared.request.value, &available)) {
            response.error = errorText("WinHttpQueryDataAvailable failed");
            return response;
        }
        if (available == 0) break;
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(prepared.request.value, buffer.data(), available, &read)) {
            response.error = errorText("WinHttpReadData failed");
            return response;
        }
        if (read == 0) break;
        if (response.body.size() + read > options.maxBytes) {
            response.error = "Response is too large.";
            return response;
        }
        response.body.append(buffer.data(), read);
        received += read;
        if (options.progress) options.progress(received, total);
    }

    if (response.status >= 200 && response.status < 300) {
        response.ok = true;
    } else {
        response.error = "HTTP " + std::to_string(response.status);
    }
    return response;
}

Response request(const std::string& url, const std::string& method,
                 const std::string& body, bool json, const Options& options) {
    PreparedRequest prepared;
    if (!prepare(url, method, body, json, options, prepared)) {
        Response response;
        response.error = prepared.error;
        return response;
    }
    return finish(prepared, options);
}

#endif

} // namespace

Response get(const std::string& url, const Options& options) {
#if defined(_WIN32)
    return request(url, "GET", {}, false, options);
#else
    (void)url;
    (void)options;
    Response response;
    response.error = "HTTP is only implemented on Windows.";
    return response;
#endif
}

Response putJson(const std::string& url, const std::string& json,
                 const Options& options) {
#if defined(_WIN32)
    return request(url, "PUT", json, true, options);
#else
    (void)url;
    (void)json;
    (void)options;
    Response response;
    response.error = "HTTP is only implemented on Windows.";
    return response;
#endif
}

bool downloadToFile(const std::string& url, const std::string& path,
                    const Options& options, std::string& error) {
    error.clear();
#if defined(_WIN32)
    PreparedRequest prepared;
    if (!prepare(url, "GET", {}, false, options, prepared)) {
        error = prepared.error;
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(prepared.request.value,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        error = "HTTP " + std::to_string(status);
        return false;
    }

    std::ofstream stream(std::filesystem::u8path(path),
                         std::ios::binary | std::ios::trunc);
    if (!stream.good()) {
        error = "Cannot create download file.";
        return false;
    }

    std::uint64_t total = prepared.contentLength;
    std::uint64_t received = 0;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(prepared.request.value, &available)) {
            error = errorText("WinHttpQueryDataAvailable failed");
            stream.close();
            fs::removeFile(path);
            return false;
        }
        if (available == 0) break;
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(prepared.request.value, buffer.data(), available, &read)) {
            error = errorText("WinHttpReadData failed");
            stream.close();
            fs::removeFile(path);
            return false;
        }
        if (read == 0) break;
        if (received + read > options.maxBytes) {
            error = "Download is too large.";
            stream.close();
            fs::removeFile(path);
            return false;
        }
        stream.write(buffer.data(), static_cast<std::streamsize>(read));
        received += read;
        if (options.progress) options.progress(received, total);
    }
    stream.flush();
    if (!stream.good()) {
        error = "Cannot write download file.";
        stream.close();
        fs::removeFile(path);
        return false;
    }
    return true;
#else
    (void)url;
    (void)path;
    (void)options;
    error = "HTTP is only implemented on Windows.";
    return false;
#endif
}

} // namespace imclash::http
