#include "core_update.h"

#include "fsutil.h"
#include "http.h"
#include "mihomo_process.h"
#include "util.h"
#include "utf8.h"

#include <algorithm>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <yyjson.h>

namespace imclash::core_update {
namespace {

#if defined(_WIN32)
std::string architectureName() {
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
        return "arm64";
    }
    return "amd64";
}

bool runTool(const std::wstring& executable, const std::wstring& arguments,
             const std::wstring& directory, std::string& error) {
    error.clear();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + executable + L"\" " + arguments;
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr,
                        directory.empty() ? nullptr : directory.c_str(),
                        &startup, &process)) {
        error = "Cannot run " + imclash::utf8::narrow(executable.c_str()) + ".";
        return false;
    }
    DWORD wait = WaitForSingleObject(process.hProcess, 60000);
    DWORD exitCode = 1;
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        wait = WaitForSingleObject(process.hProcess, 1000);
    }
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    if (wait != WAIT_OBJECT_0 || exitCode != 0) {
        error = "mihomo archive extraction failed.";
        return false;
    }
    return true;
}
#endif

bool selectAsset(yyjson_val* assets, const std::string& arch, std::string& name,
                 std::string& url) {
    if (assets == nullptr || !yyjson_is_arr(assets)) return false;
    int bestScore = -1;
    const std::size_t count = yyjson_arr_size(assets);
    for (std::size_t index = 0; index < count; ++index) {
        yyjson_val* asset = yyjson_arr_get(assets, index);
        if (asset == nullptr || !yyjson_is_obj(asset)) continue;
        yyjson_val* nameValue = yyjson_obj_get(asset, "name");
        yyjson_val* urlValue = yyjson_obj_get(asset, "browser_download_url");
        if (nameValue == nullptr || urlValue == nullptr ||
            !yyjson_is_str(nameValue) || !yyjson_is_str(urlValue)) {
            continue;
        }
        std::string candidate(yyjson_get_str(nameValue), yyjson_get_len(nameValue));
        const std::string lower = util::toLower(candidate);
        if (!util::endsWith(lower, ".zip")) continue;
        if (lower.find("mihomo") == std::string::npos) continue;
        if (lower.find("windows-" + arch) == std::string::npos) continue;
        int score = 3;
        if (lower.find("compatible") != std::string::npos) score = 1;
        if (lower.find("-go") != std::string::npos) score = std::min(score, 2);
        if (lower.find("-v1-") != std::string::npos ||
            lower.find("-v2-") != std::string::npos ||
            lower.find("-v3-") != std::string::npos) {
            score = std::min(score, 2);
        }
        if (score <= bestScore) continue;
        bestScore = score;
        name = candidate;
        url.assign(yyjson_get_str(urlValue), yyjson_get_len(urlValue));
    }
    return bestScore >= 0;
}

} // namespace

CheckResult check(const std::string& currentVersion, const std::string& proxy) {
    CheckResult result;
    result.current = currentVersion;
    http::Options options;
    options.userAgent = "imclash/0.1";
    options.accept = "application/vnd.github+json";
    options.proxy = proxy;
    options.timeoutMs = 20000;
    options.maxBytes = 8u * 1024u * 1024u;
    const http::Response response = http::get(
        "https://api.github.com/repos/MetaCubeX/mihomo/releases/latest", options);
    if (!response.ok) {
        result.error = response.error.empty() ? "Cannot check mihomo updates." : response.error;
        return result;
    }
    yyjson_doc* document = yyjson_read(response.body.data(), response.body.size(), 0);
    if (document == nullptr) {
        result.error = "Invalid GitHub release response.";
        return result;
    }
    yyjson_val* root = yyjson_doc_get_root(document);
    yyjson_val* tag = root != nullptr ? yyjson_obj_get(root, "tag_name") : nullptr;
    yyjson_val* assets = root != nullptr ? yyjson_obj_get(root, "assets") : nullptr;
    if (tag == nullptr || !yyjson_is_str(tag)) {
        result.error = "GitHub returned no release tag.";
        yyjson_doc_free(document);
        return result;
    }
    result.latest.assign(yyjson_get_str(tag), yyjson_get_len(tag));
    if (!selectAsset(assets, architectureName(), result.assetName, result.downloadUrl)) {
        result.error = "No Windows archive found in the latest mihomo release.";
        yyjson_doc_free(document);
        return result;
    }
    yyjson_doc_free(document);
    result.updateAvailable = result.current.empty() ||
                             util::compareVersions(result.latest, result.current) > 0;
    result.ok = true;
    return result;
}

DownloadResult download(const std::string& url, const std::string& version,
                        const std::string& proxy,
                        const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
    DownloadResult result;
    result.version = version;
    const std::string base = fs::tempDirectory();
    if (base.empty()) {
        result.error = "Cannot locate the temporary directory.";
        return result;
    }
    // The release tag is remote input; keep it out of filesystem paths.
    const std::string directory = fs::join(
        base, "core-" +
                  std::to_string(static_cast<unsigned long long>(GetTickCount64())));
    std::string error;
    if (!fs::ensureDirectory(directory, error)) {
        result.error = "Cannot create temporary directory: " + error;
        return result;
    }
    result.tempDirectory = directory;
    const std::string zipPath = fs::join(directory, "mihomo.zip");

    http::Options options;
    options.userAgent = "imclash/0.1";
    options.proxy = proxy;
    options.timeoutMs = 30000;
    options.maxBytes = 128u * 1024u * 1024u;
    options.progress = progress;
    if (!http::downloadToFile(url, zipPath, options, error)) {
        result.error = error.empty() ? "mihomo download failed." : error;
        fs::removeTree(directory);
        result.tempDirectory.clear();
        return result;
    }

#if defined(_WIN32)
    wchar_t systemDirectory[MAX_PATH]{};
    GetSystemDirectoryW(systemDirectory, MAX_PATH);
    const std::wstring tar = std::wstring(systemDirectory) + L"\\tar.exe";
    const std::wstring zipWide = imclash::utf8::widen(zipPath);
    const std::wstring directoryWide = imclash::utf8::widen(directory);
    const std::wstring arguments = L"-xf \"" + zipWide + L"\" -C \"" +
                                   directoryWide + L"\"";
    if (!runTool(tar, arguments, directoryWide, error)) {
        result.error = error;
        fs::removeTree(directory);
        result.tempDirectory.clear();
        return result;
    }
#endif

    std::string executablePath;
    std::error_code iterateError;
    for (std::filesystem::recursive_directory_iterator iterator(
             std::filesystem::u8path(directory), iterateError), end;
         !iterateError && iterator != end; iterator.increment(iterateError)) {
        if (!iterator->is_regular_file(iterateError) || iterateError) continue;
        const std::string name = util::toLower(iterator->path().filename().u8string());
        if (util::startsWith(name, "mihomo") && util::endsWith(name, ".exe")) {
            executablePath = iterator->path().u8string();
            break;
        }
    }
    if (executablePath.empty()) {
        result.error = "The downloaded archive does not contain a mihomo*.exe executable.";
        fs::removeTree(directory);
        result.tempDirectory.clear();
        return result;
    }

    std::string output;
    if (!mihomo_process::runCapture(executablePath, "-v", 5000, output)) {
        result.error = "The downloaded mihomo executable cannot run.";
        fs::removeTree(directory);
        result.tempDirectory.clear();
        return result;
    }
    const std::string actualVersion = util::findVersion(output);
    if (!actualVersion.empty()) result.version = actualVersion;
    result.executablePath = executablePath;
    result.ok = true;
    return result;
}

bool install(const std::string& newExecutable, std::string& error) {
    error.clear();
#if defined(_WIN32)
    const std::string target = mihomo_process::paths().core;
    if (!fs::isRegularFile(newExecutable)) {
        error = "The downloaded mihomo.exe is missing.";
        return false;
    }
    const std::wstring wideSource = imclash::utf8::widen(newExecutable);
    const std::wstring wideTarget = imclash::utf8::widen(target);
    const std::wstring wideBackup = wideTarget + L".old";
    if (wideSource.empty() || wideTarget.empty()) {
        error = "Invalid executable path.";
        return false;
    }
    DeleteFileW(wideBackup.c_str());
    const bool hadTarget = fs::isRegularFile(target);
    if (hadTarget &&
        !MoveFileExW(wideTarget.c_str(), wideBackup.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot move the running mihomo.exe aside.";
        return false;
    }
    if (!MoveFileExW(wideSource.c_str(), wideTarget.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH |
                         MOVEFILE_COPY_ALLOWED)) {
        error = "Cannot install mihomo.exe.";
        if (hadTarget) {
            MoveFileExW(wideBackup.c_str(), wideTarget.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH |
                            MOVEFILE_COPY_ALLOWED);
        }
        return false;
    }
    DeleteFileW(wideBackup.c_str());
    return true;
#else
    (void)newExecutable;
    error = "mihomo is only managed on Windows.";
    return false;
#endif
}

} // namespace imclash::core_update
