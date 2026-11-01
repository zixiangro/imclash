#include "mihomo_process.h"

#include "fsutil.h"
#include "util.h"
#include "utf8.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace imclash::mihomo_process {
namespace {

#if defined(_WIN32)
struct OutputState {
    std::mutex mutex;
    std::string text;
};

HANDLE gProcess = nullptr;
HANDLE gReadPipe = nullptr;
std::thread gReader;
const std::shared_ptr<OutputState> gOutput = std::make_shared<OutputState>();

void appendOutput(OutputState& output, const std::string& text) {
    std::lock_guard<std::mutex> lock(output.mutex);
    output.text += text;
    constexpr std::size_t kLimit = 8192;
    if (output.text.size() > kLimit) {
        output.text.erase(0, output.text.size() - kLimit);
    }
}
#endif

#if defined(_WIN32)
Paths discoverPaths() {
    const std::string defaultDirectory = fs::configDirectory();
    const std::filesystem::path profile =
        std::filesystem::u8path(defaultDirectory).parent_path().parent_path();
    const std::string localDirectory =
        (profile / ".local" / "clash").u8string();

    const auto hasConfig = [](const std::string& directory) {
        return !directory.empty() &&
               fs::isRegularFile(fs::join(directory, "config.yaml"));
    };
    const auto findCore = [](const std::string& directory) -> std::string {
        if (directory.empty()) return {};
        const std::string conventional = fs::join(directory, "mihomo.exe");
        if (fs::isRegularFile(conventional)) return conventional;

        std::vector<std::string> candidates;
        std::error_code error;
        std::filesystem::directory_iterator iterator(
            std::filesystem::u8path(directory), error);
        for (; !error && iterator != std::filesystem::directory_iterator();
             iterator.increment(error)) {
            std::error_code fileError;
            if (!iterator->is_regular_file(fileError) || fileError) continue;
            const std::string name = iterator->path().filename().u8string();
            const std::string lower = util::toLower(name);
            if (lower.rfind("mihomo", 0) == 0 && lower.size() > 4 &&
                lower.compare(lower.size() - 4, 4, ".exe") == 0) {
                candidates.push_back(iterator->path().u8string());
            }
        }
        if (candidates.empty()) return conventional;
        std::sort(candidates.begin(), candidates.end());
        return candidates.front();
    };

    Paths fallback;
    std::string chosen = defaultDirectory;
    if (!hasConfig(chosen) && hasConfig(localDirectory)) chosen = localDirectory;
    fallback.directory = chosen;
    if (!chosen.empty()) {
        fallback.config = fs::join(chosen, "config.yaml");
        fallback.core = findCore(chosen);
    }

    // Prefer the directory of a running mihomo. This lets imclash manage an
    // existing installation (for example ~/.local/clash) without a setting.
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return fallback;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry) != FALSE) {
        do {
            std::wstring name = entry.szExeFile;
            std::transform(name.begin(), name.end(), name.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            if (name.rfind(L"mihomo", 0) != 0) continue;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                         entry.th32ProcessID);
            if (process == nullptr) continue;
            std::vector<wchar_t> buffer(32768);
            DWORD size = static_cast<DWORD>(buffer.size());
            const BOOL resolved = QueryFullProcessImageNameW(
                process, 0, buffer.data(), &size);
            CloseHandle(process);
            if (!resolved) continue;

            const std::filesystem::path executable(
                std::wstring(buffer.data(), size));
            const std::filesystem::path directory = executable.parent_path();
            const std::filesystem::path config = directory / L"config.yaml";
            std::error_code error;
            if (!std::filesystem::is_regular_file(config, error) || error) {
                continue;
            }

            Paths result;
            result.directory = directory.u8string();
            result.config = config.u8string();
            result.core = executable.u8string();
            CloseHandle(snapshot);
            return result;
        } while (Process32NextW(snapshot, &entry) != FALSE);
    }
    CloseHandle(snapshot);
    return fallback;
}

void closeProcessHandles() {
    // The reader owns the pipe's read end. The process has already exited or
    // has been terminated at every call site, so joining first cannot block.
    if (gReader.joinable()) gReader.join();
    if (gReadPipe != nullptr) {
        CloseHandle(gReadPipe);
        gReadPipe = nullptr;
    }
    if (gProcess != nullptr) {
        CloseHandle(gProcess);
        gProcess = nullptr;
    }
}
#else
Paths discoverPaths() {
    Paths result;
    result.directory = fs::configDirectory();
    if (!result.directory.empty()) {
        result.config = fs::join(result.directory, "config.yaml");
        result.core = fs::join(result.directory, "mihomo.exe");
    }
    return result;
}
#endif

} // namespace

Paths paths() {
    static const Paths discovered = discoverPaths();
    return discovered;
}

bool runCapture(const std::string& executable, const std::string& arguments,
                int timeoutMs, std::string& output) {
    output.clear();
#if defined(_WIN32)
    const std::wstring wideExecutable = imclash::utf8::widen(executable);
    const std::wstring wideArguments = imclash::utf8::widen(arguments);
    const std::wstring wideDirectory = imclash::utf8::widen(paths().directory);
    if (wideExecutable.empty()) return false;

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &attributes, 0)) return false;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    startup.hStdInput = nullptr;

    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + wideExecutable + L"\"";
    if (!wideArguments.empty()) {
        commandLine += L" ";
        commandLine += wideArguments;
    }
    const BOOL started = CreateProcessW(
        wideExecutable.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,
        wideDirectory.empty() ? nullptr : wideDirectory.c_str(), &startup, &process);
    CloseHandle(writeEnd);
    if (!started) {
        CloseHandle(readEnd);
        return false;
    }

    constexpr std::size_t kMaxOutput = 64u * 1024u;
    const ULONGLONG deadline = GetTickCount64() +
        static_cast<ULONGLONG>(timeoutMs > 0 ? timeoutMs : 10000);
    char buffer[4096];
    bool finished = false;
    while (GetTickCount64() < deadline) {
        DWORD available = 0;
        if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available == 0) {
            if (WaitForSingleObject(process.hProcess, 20) == WAIT_OBJECT_0) {
                while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) &&
                       available > 0) {
                    DWORD read = 0;
                    if (!ReadFile(readEnd, buffer,
                                  static_cast<DWORD>(std::min<std::size_t>(available,
                                                                          sizeof(buffer))),
                                  &read, nullptr) || read == 0) {
                        break;
                    }
                    output.append(buffer, read);
                    if (output.size() > kMaxOutput) break;
                }
                finished = true;
                break;
            }
            continue;
        }
        DWORD read = 0;
        if (!ReadFile(readEnd, buffer,
                      static_cast<DWORD>(std::min<std::size_t>(available, sizeof(buffer))),
                      &read, nullptr) || read == 0) {
            break;
        }
        output.append(buffer, read);
        if (output.size() > kMaxOutput) break;
    }
    if (!finished) {
        // A quick helper may close its pipe before PeekNamedPipe observes it.
        // Give it a short grace period and trust the exit code.
        if (WaitForSingleObject(process.hProcess, 500) == WAIT_OBJECT_0) {
            finished = true;
        } else {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 1000);
        }
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(readEnd);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    output = imclash::utf8::fromConsoleCodePage(output);
    return finished && exitCode == 0;
#else
    (void)executable;
    (void)arguments;
    (void)timeoutMs;
    return false;
#endif
}

std::string coreVersion() {
    const Paths current = paths();
    if (!fs::isRegularFile(current.core)) return {};
    std::string output;
    if (!runCapture(current.core, "-v", 5000, output)) return {};
    const std::string version = util::findVersion(output);
    return version.empty() ? util::trim(output) : version;
}

bool testConfig(const std::string& configPath, std::string& output) {
    const Paths current = paths();
    if (!fs::isRegularFile(current.core)) {
        output = "mihomo.exe was not found.";
        return false;
    }
    const std::string arguments = "-t -f \"" + configPath + "\" -d \"" +
                                  current.directory + "\"";
    const bool ok = runCapture(current.core, arguments, 20000, output);
    if (!ok && util::trim(output).empty()) {
        output = "mihomo rejected the configuration.";
    }
    return ok;
}

StartResult start(const config::Info& info) {
    StartResult result;
#if defined(_WIN32)
    const Paths current = paths();
    if (!fs::isRegularFile(current.core)) {
        result.error = "mihomo.exe was not found.";
        return result;
    }
    if (!fs::isRegularFile(current.config)) {
        result.error = "config.yaml was not found.";
        return result;
    }
    if (gProcess != nullptr) {
        if (WaitForSingleObject(gProcess, 0) == WAIT_TIMEOUT) {
            result.error = "mihomo is already running.";
            return result;
        }
        closeProcessHandles();
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &attributes, 0)) {
        result.error = "Cannot create mihomo output pipe.";
        return result;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    startup.hStdInput = nullptr;

    std::wstring arguments = L"-d \"" + imclash::utf8::widen(current.directory) + L"\"";
    if (info.externalController.empty()) {
        arguments += L" -ext-ctl 127.0.0.1:9090";
    }
    const std::wstring wideCore = imclash::utf8::widen(current.core);
    const std::wstring wideDirectory = imclash::utf8::widen(current.directory);
    std::wstring commandLine = L"\"" + wideCore + L"\" " + arguments;

    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(
        wideCore.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,
        wideDirectory.empty() ? nullptr : wideDirectory.c_str(), &startup, &process);
    CloseHandle(writeEnd);
    if (!started) {
        CloseHandle(readEnd);
        result.error = "Cannot start mihomo (error " +
                       std::to_string(static_cast<unsigned long>(GetLastError())) + ").";
        return result;
    }

    gReadPipe = readEnd;
    gProcess = process.hProcess;
    CloseHandle(process.hThread);

    {
        std::lock_guard<std::mutex> lock(gOutput->mutex);
        gOutput->text.clear();
    }
    const std::shared_ptr<OutputState> outputState = gOutput;
    gReader = std::thread([readEnd, outputState] {
        char buffer[4096];
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) || read == 0) {
                break;
            }
            appendOutput(*outputState, std::string(buffer, read));
        }
    });

    result.ok = true;
    return result;
#else
    (void)info;
    result.error = "mihomo is only managed on Windows.";
    return result;
#endif
}

void stop() {
#if defined(_WIN32)
    if (gProcess != nullptr) {
        TerminateProcess(gProcess, 0);
        WaitForSingleObject(gProcess, 3000);
    }
    closeProcessHandles();
#endif
}

bool stopRunning(std::string& error) {
    error.clear();
#if defined(_WIN32)
    if (gProcess != nullptr) {
        stop();
        return true;
    }

    const std::string wanted = util::toLower(paths().core);
    if (wanted.empty()) return true;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry) != FALSE) {
        do {
            std::wstring name = entry.szExeFile;
            std::transform(name.begin(), name.end(), name.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            if (name.rfind(L"mihomo", 0) != 0) continue;

            HANDLE process = OpenProcess(
                PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE, entry.th32ProcessID);
            if (process == nullptr) continue;
            std::vector<wchar_t> buffer(32768);
            DWORD size = static_cast<DWORD>(buffer.size());
            if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) {
                CloseHandle(process);
                continue;
            }
            const std::string executable = util::toLower(
                std::filesystem::path(std::wstring(buffer.data(), size)).u8string());
            if (executable != wanted) {
                CloseHandle(process);
                continue;
            }

            if (!TerminateProcess(process, 0)) {
                error = "Cannot stop the running mihomo (error " +
                        std::to_string(static_cast<unsigned long>(GetLastError())) + ").";
                CloseHandle(process);
                break;
            }
            const DWORD wait = WaitForSingleObject(process, 5000);
            CloseHandle(process);
            if (wait != WAIT_OBJECT_0) {
                error = "mihomo did not exit in time.";
            }
            break;
        } while (Process32NextW(snapshot, &entry) != FALSE);
    }
    CloseHandle(snapshot);
    return error.empty();
#else
    error = "mihomo is only managed on Windows.";
    return false;
#endif
}

bool poll(std::string& output) {
#if defined(_WIN32)
    if (gProcess == nullptr) return false;
    if (WaitForSingleObject(gProcess, 0) != WAIT_OBJECT_0) return false;
    DWORD exitCode = 0;
    GetExitCodeProcess(gProcess, &exitCode);
    output = outputTail();
    if (output.empty()) {
        output = "mihomo exited with code " + std::to_string(exitCode) + ".";
    }
    closeProcessHandles();
    return true;
#else
    (void)output;
    return false;
#endif
}

std::string outputTail() {
#if defined(_WIN32)
    std::string text;
    {
        std::lock_guard<std::mutex> lock(gOutput->mutex);
        text = gOutput->text;
    }
    return imclash::utf8::fromConsoleCodePage(text);
#else
    return {};
#endif
}

void detachForExit() {
#if defined(_WIN32)
    // The core outlives this process, so its output pipe never reaches EOF.
    // Detach instead of joining; the OS closes the pipe when the process exits.
    if (gReader.joinable()) gReader.detach();
#endif
}

} // namespace imclash::mihomo_process
