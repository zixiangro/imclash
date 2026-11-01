#include "fsutil.h"

#include "utf8.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace imclash::fs {
namespace {

#if defined(_WIN32)
std::string wideToUtf8(const std::wstring& text) {
    return imclash::utf8::narrow(text.c_str());
}

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path)) ||
        path == nullptr) {
        if (path != nullptr) CoTaskMemFree(path);
        return {};
    }
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

std::string windowsError(const char* prefix) {
    const DWORD code = GetLastError();
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s (error %lu)", prefix,
                  static_cast<unsigned long>(code));
    return buffer;
}
#endif

} // namespace

std::string configDirectory() {
#if defined(_WIN32)
    const std::wstring profile = knownFolder(FOLDERID_Profile);
    if (profile.empty()) return {};
    std::filesystem::path path(profile);
    path /= L".config";
    path /= L"mihomo";
    return path.u8string();
#else
    return {};
#endif
}

std::string tempDirectory() {
    std::error_code error;
    std::filesystem::path base = std::filesystem::temp_directory_path(error);
    if (error) return {};
    base /= "imclash";
    return base.u8string();
}

std::string join(const std::string& directory, const std::string& name) {
    return (std::filesystem::u8path(directory) / std::filesystem::u8path(name)).u8string();
}

std::string fileName(const std::string& path) {
    return std::filesystem::u8path(path).filename().u8string();
}

bool readText(const std::string& path, std::string& output, std::string& error) {
    output.clear();
    error.clear();
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(std::filesystem::u8path(path), sizeError);
    if (sizeError) {
        error = "Cannot read file size: " + sizeError.message();
        return false;
    }
    constexpr std::uintmax_t kMaxFile = 64u * 1024u * 1024u;
    if (size > kMaxFile) {
        error = "File is too large (limit 64 MB).";
        return false;
    }
    std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
    if (!stream.good()) {
        error = "Cannot open file.";
        return false;
    }
    output.resize(static_cast<std::size_t>(size));
    if (size > 0) stream.read(output.data(), static_cast<std::streamsize>(size));
    if (!stream.good() && !stream.eof()) {
        error = "Cannot read file.";
        output.clear();
        return false;
    }
    return true;
}

bool writeTextAtomic(const std::string& path, const std::string& content,
                     std::string& error) {
    error.clear();
    const std::filesystem::path target = std::filesystem::u8path(path);
    const std::filesystem::path temporary = std::filesystem::u8path(path + ".tmp");
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.good()) {
            error = "Cannot create temporary file.";
            return false;
        }
        if (!content.empty()) {
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        stream.flush();
        if (!stream.good()) {
            error = "Cannot write temporary file.";
            stream.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
#if defined(_WIN32)
    const std::wstring wideTemporary = imclash::utf8::widen(temporary.u8string());
    const std::wstring wideTarget = imclash::utf8::widen(target.u8string());
    if (wideTemporary.empty() || wideTarget.empty() ||
        !MoveFileExW(wideTemporary.c_str(), wideTarget.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = windowsError("Cannot replace file");
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
#else
    std::error_code renameError;
    std::filesystem::rename(temporary, target, renameError);
    if (renameError) {
        error = renameError.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
#endif
    return true;
}

bool ensureDirectory(const std::string& path, std::string& error) {
    error.clear();
    if (path.empty()) {
        error = "Empty directory path.";
        return false;
    }
    std::error_code createError;
    std::filesystem::create_directories(std::filesystem::u8path(path), createError);
    if (createError) {
        error = createError.message();
        return false;
    }
    return true;
}

bool exists(const std::string& path) {
    std::error_code error;
    return std::filesystem::exists(std::filesystem::u8path(path), error) && !error;
}

bool isRegularFile(const std::string& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::u8path(path), error) && !error;
}

std::uint64_t fileSize(const std::string& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(std::filesystem::u8path(path), error);
    return error ? 0 : static_cast<std::uint64_t>(size);
}

std::int64_t modifiedSeconds(const std::string& path) {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA data{};
    const std::wstring wide = imclash::utf8::widen(path);
    if (wide.empty() || !GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &data)) {
        return 0;
    }
    ULARGE_INTEGER value{};
    value.LowPart = data.ftLastWriteTime.dwLowDateTime;
    value.HighPart = data.ftLastWriteTime.dwHighDateTime;
    constexpr unsigned long long kEpochDifference = 116444736000000000ULL;
    if (value.QuadPart < kEpochDifference) return 0;
    return static_cast<std::int64_t>((value.QuadPart - kEpochDifference) / 10000000ULL);
#else
    (void)path;
    return 0;
#endif
}

bool removeFile(const std::string& path) {
    std::error_code error;
    return std::filesystem::remove(std::filesystem::u8path(path), error) && !error;
}

bool removeTree(const std::string& path) {
    std::error_code error;
    std::filesystem::remove_all(std::filesystem::u8path(path), error);
    return !error;
}

bool replaceFile(const std::string& source, const std::string& target,
                 std::string& error) {
    error.clear();
#if defined(_WIN32)
    const std::wstring wideSource = imclash::utf8::widen(source);
    const std::wstring wideTarget = imclash::utf8::widen(target);
    if (wideSource.empty() || wideTarget.empty()) {
        error = "Invalid file path.";
        return false;
    }
    if (!MoveFileExW(wideSource.c_str(), wideTarget.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = windowsError("Cannot replace file");
        return false;
    }
    return true;
#else
    std::error_code renameError;
    std::filesystem::rename(std::filesystem::u8path(source),
                            std::filesystem::u8path(target), renameError);
    if (renameError) {
        error = renameError.message();
        return false;
    }
    return true;
#endif
}

} // namespace imclash::fs
