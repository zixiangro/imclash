#include "shell_actions.h"

#include "utf8.h"

#include <filesystem>
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
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

namespace imclash::shell {
namespace {

#if defined(_WIN32)
bool invokeVerb(const std::string& path, const wchar_t* verb, ULONG mask) {
    const std::wstring wide = imclash::utf8::widen(path);
    if (wide.empty()) return false;
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = mask;
    info.lpVerb = verb;
    info.lpFile = wide.c_str();
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) != FALSE;
}

constexpr const wchar_t* kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kValueName = L"imclash";
#endif

} // namespace

namespace {

std::string executablePath() {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1) {
            return imclash::utf8::narrow(buffer.data());
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    return {};
#endif
}

std::string executableDirectory() {
    const std::string path = executablePath();
    if (path.empty()) return {};
    return std::filesystem::u8path(path).parent_path().u8string();
}

} // namespace

void setWorkingDirectoryToExecutable() {
#if defined(_WIN32)
    const std::string directory = executableDirectory();
    const std::wstring wide = imclash::utf8::widen(directory);
    if (!wide.empty()) SetCurrentDirectoryW(wide.c_str());
#endif
}

bool openFolder(const std::string& path) {
#if defined(_WIN32)
    return invokeVerb(path, L"open", SEE_MASK_FLAG_NO_UI);
#else
    (void)path;
    return false;
#endif
}

bool openInEditor(const std::string& path) {
#if defined(_WIN32)
    if (invokeVerb(path, L"edit", SEE_MASK_FLAG_NO_UI)) return true;
    return invokeVerb(path, L"open", SEE_MASK_FLAG_NO_UI);
#else
    (void)path;
    return false;
#endif
}

bool isAutostartEnabled() {
#if defined(_WIN32)
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS status = RegQueryValueExW(key, kValueName, nullptr, &type,
                                            nullptr, &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ && size > 0;
#else
    return false;
#endif
}

bool setAutostartEnabled(bool enabled) {
#if defined(_WIN32)
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    bool ok = false;
    if (enabled) {
        const std::string path = executablePath();
        if (!path.empty()) {
            std::wstring command = L"\"";
            command += imclash::utf8::widen(path);
            command += L"\" --autostart";
            const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
            ok = RegSetValueExW(key, kValueName, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                bytes) == ERROR_SUCCESS;
        }
    } else {
        const LSTATUS status = RegDeleteValueW(key, kValueName);
        ok = status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
#else
    (void)enabled;
    return false;
#endif
}

} // namespace imclash::shell
