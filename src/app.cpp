// imclash - single-subscription manager for a local mihomo installation.
//
// All persistent subscription state lives in mihomo's own directory:
//   %USERPROFILE%\.config\mihomo\config.yaml
// The update URL is the first line of that file, written as "# url: ...".

#include "eui_neo.h"
#include "eui/async.h"

#include "config.h"
#include "core_update.h"
#include "fsutil.h"
#include "http.h"
#include "mihomo_api.h"
#include "mihomo_process.h"
#include "shell_actions.h"
#include "util.h"
#include "window_frame.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
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
#endif

namespace app {

using namespace imclash;

namespace {

constexpr float kTitleBarHeight = 36.0f;
constexpr float kMargin = 16.0f;
constexpr float kCardRadius = 5.0f;
constexpr float kRowHeight = 28.0f;
constexpr float kStatusBarHeight = 26.0f;
constexpr float kMinimizeWidth = 36.0f;
constexpr float kMenuButtonHeight = 24.0f;
constexpr float kMihomoButtonWidth = 72.0f;
constexpr float kSubscriptionButtonWidth = 100.0f;
constexpr float kProxyChipWidth = 104.0f;
constexpr float kStartupChipWidth = 104.0f;

const eui::Color kBackground{0.122f, 0.122f, 0.122f, 1.0f};
const eui::Color kPanel{0.129f, 0.129f, 0.129f, 1.0f};
const eui::Color kPanelRaised{0.157f, 0.157f, 0.157f, 1.0f};
const eui::Color kTitleBar{0.094f, 0.094f, 0.094f, 1.0f};
const eui::Color kBorder{0.169f, 0.169f, 0.169f, 1.0f};
const eui::Color kBorderStrong{0.235f, 0.235f, 0.235f, 1.0f};
const eui::Color kTextPrimary{0.800f, 0.800f, 0.800f, 1.0f};
const eui::Color kTextSecondary{0.616f, 0.616f, 0.616f, 1.0f};
const eui::Color kAccent{0.0f, 0.471f, 0.831f, 1.0f};
const eui::Color kAccentHover{0.008f, 0.431f, 0.757f, 1.0f};
const eui::Color kAccentPressed{0.0f, 0.376f, 0.663f, 1.0f};
const eui::Color kSuccess{0.306f, 0.788f, 0.690f, 1.0f};
const eui::Color kWarning{0.800f, 0.655f, 0.0f, 1.0f};
const eui::Color kError{0.945f, 0.298f, 0.298f, 1.0f};
const eui::Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};
const eui::Color kChipIdle{0.0f, 0.0f, 0.0f, 0.0f};
const eui::Color kChipHover{1.0f, 1.0f, 1.0f, 0.08f};
const eui::Color kChipPressed{1.0f, 1.0f, 1.0f, 0.14f};
const eui::Color kRowHover{1.0f, 1.0f, 1.0f, 0.055f};

constexpr const char* kTestUrl = "https://www.gstatic.com/generate_204";

enum class MenuKind {
    None,
    Subscription,
    Mihomo,
};

enum class DialogKind {
    None,
    AddUrl,
    ImportFile,
    ConfirmReplace,
    CoreUpdate,
    Error,
};

enum class PendingKind {
    None,
    UrlDownload,
    FileImport,
};

struct NodeEntry {
    std::string name;
    std::string type;
    int delay = -1;
    int defaultOrder = 0;
    bool testing = false;
    bool tested = false;
};

struct ProxySnapshot {
    std::vector<mihomo_api::Node> nodes;
    std::vector<mihomo_api::Group> groups;
};

struct SwitchResult {
    int switched = 0;
    int total = 0;
    bool mainSwitched = false;
    std::string error;
};

struct AppState {
    std::string configDir;
    std::string configPath;
    std::string corePath;

    bool configPresent = false;
    config::Info configInfo;
    std::string sourceUrl;
    std::uint64_t configBytes = 0;
    std::int64_t configModified = 0;

    bool corePresent = false;
    bool coreRunning = false;
    bool coreOwned = false;
    bool coreStarting = false;
    bool apiReady = false;
    std::string coreVersion;
    std::string coreOutput;
    std::chrono::steady_clock::time_point coreStartedAt{};
    std::chrono::steady_clock::time_point nextPing{};
    std::chrono::steady_clock::time_point nextConfigCheck{};
    bool pingInFlight = false;
    bool startupProbeDone = false;
    int pingGeneration = 0;
    int nodesGeneration = 0;

    bool proxyAvailable = false;
    bool viaProxy = false;
    bool proxyUserTouched = false;
    bool proxyUserWants = true;
    bool autostart = false;

    std::vector<NodeEntry> nodes;
    std::vector<mihomo_api::Group> groups;
    std::string mainGroup;
    std::string mainGroupNow;
    std::string currentNode;
    bool selectingNode = false;
    int selectGeneration = 0;
    bool nodesLoaded = false;
    bool nodesLoading = false;
    std::string nodesError;
    bool testingAll = false;
    int testGeneration = 0;
    int testPending = 0;
    int testDone = 0;
    int testTotal = 0;
    bool sortBySpeed = false;
    float nodeScroll = 0.0f;

    MenuKind menu = MenuKind::None;
    float menuX = 0.0f;
    float menuY = 0.0f;
    DialogKind dialog = DialogKind::None;

    std::string addUrl;
    std::string importPath;
    std::string importContent;
    std::string importUrl;
    std::string dialogError;

    PendingKind pendingKind = PendingKind::None;
    std::string pendingUrl;
    std::string pendingPath;
    std::string pendingContent;
    std::string pendingImportUrl;

    core_update::CheckResult coreCheck;
    bool coreCheckValid = false;
    bool coreUpdateInFlight = false;
    int coreUpdateGeneration = 0;
    std::chrono::steady_clock::time_point coreUpdateStartedAt{};

    bool busy = false;
    std::string busyText;
    std::string status;
    std::string errorTitle;
    std::string errorText;

    float listX = 0.0f;
    float listY = 0.0f;
    float listW = 0.0f;
    float listH = 0.0f;
};

AppState& state() {
    static AppState instance;
    return instance;
}

bool hasAutostartArgument() {
#if defined(_WIN32)
    const std::wstring command = GetCommandLineW();
    return command.find(L"--autostart") != std::wstring::npos;
#else
    return false;
#endif
}

void ensureSingleInstance() {
#if defined(_WIN32)
    static HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\imclash-single");
    if (mutex == nullptr) return;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // EUI has already created this process's own window before onStart
        // runs, so skip windows owned by this process when looking for the
        // instance that already holds the mutex.
        HWND existing = nullptr;
        HWND candidate = nullptr;
        while ((candidate = FindWindowExW(nullptr, candidate, L"GLFW30", L"imclash")) !=
               nullptr) {
            DWORD processId = 0;
            GetWindowThreadProcessId(candidate, &processId);
            if (processId != GetCurrentProcessId()) {
                existing = candidate;
                break;
            }
        }
        if (!hasAutostartArgument() && existing != nullptr) {
            const UINT message = RegisterWindowMessageW(L"imclash-show");
            if (message != 0) PostMessageW(existing, message, 0, 0);
        }
        ExitProcess(0);
    }
#endif
}

void setStatus(std::string text) {
    state().status = std::move(text);
    requestUpdate();
}

void showError(const std::string& title, const std::string& text) {
    state().errorTitle = title;
    state().errorText = text.empty() ? "Unknown error." : text;
    state().dialog = DialogKind::Error;
    state().menu = MenuKind::None;
    imclash::frame::setTitleBarDragEnabled(false);
    requestUpdate();
}

void closeDialog() {
    state().dialog = DialogKind::None;
    state().dialogError.clear();
    imclash::frame::setTitleBarDragEnabled(state().menu == MenuKind::None);
    requestUpdate();
}

void openDialog(DialogKind kind) {
    state().dialog = kind;
    state().menu = MenuKind::None;
    state().dialogError.clear();
    imclash::frame::setTitleBarDragEnabled(false);
    requestUpdate();
}

void setMenuOpen(MenuKind kind, float x, float y) {
    state().menu = kind;
    state().menuX = x;
    state().menuY = y;
    imclash::frame::setTitleBarDragEnabled(kind == MenuKind::None &&
                                           state().dialog == DialogKind::None);
    requestUpdate();
}

bool proxyOn() {
    return state().proxyAvailable && state().viaProxy;
}

std::string proxyForDownload() {
    if (!proxyOn()) return {};
    return config::proxyAddress(state().configInfo);
}

void updateProxyState() {
    AppState& appState = state();
    appState.proxyAvailable =
        appState.coreRunning && !config::proxyAddress(appState.configInfo).empty();
    if (!appState.proxyAvailable) {
        appState.viaProxy = false;
    } else {
        appState.viaProxy = appState.proxyUserTouched ? appState.proxyUserWants : true;
    }
}

void resetCoreDerivedState() {
    AppState& appState = state();
    // In-flight API calls belong to the old process. Their completions must not
    // overwrite the state of a replacement core.
    async::cancel("imclash.startup.ping");
    async::cancel("imclash.ping");
    async::cancel("imclash.nodes");
    ++appState.pingGeneration;
    ++appState.nodesGeneration;
    appState.pingInFlight = false;
    appState.coreRunning = false;
    appState.coreOwned = false;
    appState.coreStarting = false;
    appState.apiReady = false;
    appState.nodesLoading = false;
    appState.nodesLoaded = false;
    appState.nodesError.clear();
    appState.nodes.clear();
    appState.groups.clear();
    appState.mainGroup.clear();
    appState.mainGroupNow.clear();
    appState.currentNode.clear();
    appState.selectingNode = false;
    ++appState.selectGeneration;
    appState.testingAll = false;
    ++appState.testGeneration;
    updateProxyState();
}

const mihomo_api::Group* chooseMainGroup(
    const std::vector<mihomo_api::Group>& groups) {
    const auto isSelector = [](const mihomo_api::Group& group) {
        return util::toLower(group.type) == "selector" &&
               util::toLower(group.name) != "global";
    };
    for (const auto& group : groups) {
        if (!isSelector(group)) continue;
        if (group.name.find("节点选择") != std::string::npos) return &group;
    }
    for (const auto& group : groups) {
        if (!isSelector(group)) continue;
        const std::string lower = util::toLower(group.name);
        if (lower == "proxy" || lower == "proxies" || lower == "proxy group") {
            return &group;
        }
    }
    const mihomo_api::Group* largest = nullptr;
    for (const auto& group : groups) {
        if (!isSelector(group) || group.all.empty()) continue;
        if (largest == nullptr || group.all.size() > largest->all.size()) {
            largest = &group;
        }
    }
    if (largest != nullptr) return largest;
    for (const auto& group : groups) {
        if (isSelector(group)) return &group;
    }
    return nullptr;
}

std::string resolveCurrentNode(
    const std::string& start, const std::vector<mihomo_api::Group>& groups) {
    std::string name = start;
    for (int depth = 0; depth < 8; ++depth) {
        const mihomo_api::Group* group = nullptr;
        for (const auto& candidate : groups) {
            if (candidate.name == name) {
                group = &candidate;
                break;
            }
        }
        if (group == nullptr || group->now.empty() || group->now == name) break;
        name = group->now;
    }
    return name;
}

void refreshConfigState() {
    AppState& appState = state();
    appState.configInfo = config::read(appState.configPath);
    appState.configPresent = appState.configInfo.present;
    appState.sourceUrl = appState.configInfo.sourceUrl;
    appState.configBytes = appState.configPresent ? fs::fileSize(appState.configPath) : 0;
    appState.configModified = appState.configPresent ? fs::modifiedSeconds(appState.configPath) : 0;
    updateProxyState();
}

void startCore();
void loadNodes();
void startPing();
void startupProbe();

void tick() {
    AppState& appState = state();
    const auto now = std::chrono::steady_clock::now();

    std::string output;
    if (mihomo_process::poll(output)) {
        appState.coreOutput = output;
        resetCoreDerivedState();
        showError("mihomo exited", output);
    }

    if (appState.coreUpdateInFlight &&
        now - appState.coreUpdateStartedAt > std::chrono::seconds(120)) {
        ++appState.coreUpdateGeneration;
        appState.coreUpdateInFlight = false;
        async::cancel("imclash.core.download");
        appState.busy = false;
        appState.busyText.clear();
        showError("mihomo update timed out",
                  "The download did not respond within 120 seconds. "
                  "The running core was not stopped.");
    }

    if ((appState.coreRunning || appState.coreStarting) && !appState.pingInFlight &&
        now >= appState.nextPing) {
        startPing();
    }

    if (now >= appState.nextConfigCheck) {
        appState.nextConfigCheck = now + std::chrono::seconds(2);
        const std::int64_t modified = fs::modifiedSeconds(appState.configPath);
        if (modified != appState.configModified) {
            refreshConfigState();
        }
    }

    app::requestUpdate();
}

void startPing() {
    AppState& appState = state();
    if (appState.pingInFlight || (!appState.coreRunning && !appState.coreStarting)) {
        return;
    }
    appState.pingInFlight = true;
    const int generation = appState.pingGeneration;
    appState.nextPing = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    const config::Info info = appState.configInfo;
    const auto startedAt = appState.coreStartedAt;
    const bool owned = appState.coreOwned;

    async::restart(
        "imclash.ping",
        [info]() -> async::Result<std::string> {
            std::string version;
            std::string error;
            if (!mihomo_api::ping(info, version, error)) {
                return async::failure<std::string>(error);
            }
            return async::success(std::move(version));
        },
        [info, startedAt, owned, generation](const async::Result<std::string>& result) {
            AppState& appState = state();
            if (generation != appState.pingGeneration) return;
            appState.pingInFlight = false;
            if (result.ok) {
                const bool firstReady = !appState.apiReady;
                appState.apiReady = true;
                appState.coreRunning = true;
                appState.coreVersion = result.value;
                appState.coreStarting = false;
                updateProxyState();
                if (firstReady && appState.coreOwned) appState.status = "mihomo is running.";
                if (firstReady || !appState.nodesLoaded) loadNodes();
            } else {
                appState.apiReady = false;
                if (appState.coreStarting) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - startedAt);
                    if (elapsed.count() >= 12) {
                        appState.coreStarting = false;
                        appState.coreOutput = mihomo_process::outputTail();
                        showError("mihomo API is not ready",
                                  appState.coreOutput.empty()
                                      ? "mihomo started but its external controller did not answer."
                                      : appState.coreOutput);
                    }
                } else if (!owned) {
                    appState.coreRunning = false;
                    updateProxyState();
                    appState.status = "mihomo API is not reachable.";
                }
            }
            app::requestUpdate();
        });
}

void startCore() {
    AppState& appState = state();
    if (!appState.configPresent) {
        showError("No subscription", "Add a subscription before starting mihomo.");
        return;
    }
    if (!appState.corePresent) {
        showError("mihomo.exe not found",
                  "Put mihomo.exe in " + appState.configDir +
                      " or download it from the Mihomo menu.");
        return;
    }
    if (appState.coreRunning) return;

    const mihomo_process::StartResult result =
        mihomo_process::start(appState.configInfo);
    if (!result.ok) {
        showError("Cannot start mihomo", result.error);
        return;
    }
    appState.coreRunning = true;
    appState.coreOwned = true;
    appState.coreStarting = true;
    appState.apiReady = false;
    appState.coreStartedAt = std::chrono::steady_clock::now();
    appState.nextPing = appState.coreStartedAt;
    appState.pingInFlight = false;
    updateProxyState();
    setStatus("Starting mihomo...");
}

void stopCore() {
    AppState& appState = state();
    if (!appState.coreOwned) {
        showError("Cannot stop mihomo",
                  "This mihomo instance was not started by imclash.");
        return;
    }
    mihomo_process::stop();
    resetCoreDerivedState();
    setStatus("mihomo stopped.");
}

void restartCore() {
    AppState& appState = state();
    if (!appState.coreOwned) {
        showError("Cannot restart mihomo",
                  "This mihomo instance was not started by imclash.");
        return;
    }
    mihomo_process::stop();
    resetCoreDerivedState();
    startCore();
}

void loadNodes() {
    AppState& appState = state();
    if (!appState.apiReady || appState.nodesLoading) return;
    appState.nodesLoading = true;
    const int generation = ++appState.nodesGeneration;
    appState.nodesError.clear();
    appState.selectingNode = false;
    ++appState.selectGeneration;
    const config::Info info = appState.configInfo;

    async::restart(
        "imclash.nodes",
        [info]() -> async::Result<ProxySnapshot> {
            ProxySnapshot snapshot;
            std::string error;
            if (!mihomo_api::fetchState(info, snapshot.nodes, snapshot.groups, error)) {
                return async::failure<ProxySnapshot>(error);
            }
            return async::success(std::move(snapshot));
        },
        [info, generation](const async::Result<ProxySnapshot>& result) {
            AppState& appState = state();
            if (generation != appState.nodesGeneration) return;
            appState.nodesLoading = false;
            if (!result.ok) {
                appState.nodesError = result.error;
                appState.nodesLoaded = false;
                app::requestUpdate();
                return;
            }

            appState.groups = result.value.groups;
            const mihomo_api::Group* main = chooseMainGroup(appState.groups);
            appState.mainGroup = main != nullptr ? main->name : std::string();
            appState.mainGroupNow = main != nullptr ? main->now : std::string();
            appState.currentNode = main != nullptr
                ? resolveCurrentNode(main->now, appState.groups)
                : std::string();

            std::vector<NodeEntry> entries;
            entries.reserve(result.value.nodes.size());
            for (std::size_t index = 0; index < result.value.nodes.size(); ++index) {
                const mihomo_api::Node& source = result.value.nodes[index];
                NodeEntry entry;
                entry.name = source.name;
                entry.type = source.type;
                entry.delay = source.delay;
                entry.tested = source.delay > 0;
                const auto order = std::find(info.proxyOrder.begin(),
                                             info.proxyOrder.end(), source.name);
                entry.defaultOrder = order != info.proxyOrder.end()
                    ? static_cast<int>(order - info.proxyOrder.begin())
                    : static_cast<int>(info.proxyOrder.size() + index);
                entries.push_back(std::move(entry));
            }
            appState.nodes = std::move(entries);
            appState.nodesLoaded = true;
            appState.nodeScroll = 0.0f;
            app::requestUpdate();
        });
}

std::vector<NodeEntry*> sortedNodes() {
    AppState& appState = state();
    std::vector<NodeEntry*> result;
    result.reserve(appState.nodes.size());
    for (NodeEntry& node : appState.nodes) result.push_back(&node);

    const std::string current = appState.currentNode;
    const bool sortBySpeed = appState.sortBySpeed;
    std::sort(result.begin(), result.end(),
              [current, sortBySpeed](const NodeEntry* left, const NodeEntry* right) {
        const bool leftCurrent = !current.empty() && left->name == current;
        const bool rightCurrent = !current.empty() && right->name == current;
        if (leftCurrent != rightCurrent) return leftCurrent;
        if (sortBySpeed) {
            const bool leftValid = left->delay > 0;
            const bool rightValid = right->delay > 0;
            if (leftValid != rightValid) return leftValid;
            if (leftValid && rightValid && left->delay != right->delay) {
                return left->delay < right->delay;
            }
            return left->defaultOrder < right->defaultOrder;
        }
        if (left->defaultOrder != right->defaultOrder) {
            return left->defaultOrder < right->defaultOrder;
        }
        return left->name < right->name;
    });
    return result;
}

void testAllNodes() {
    AppState& appState = state();
    if (!appState.apiReady || appState.nodes.empty() || appState.testingAll) return;
    ++appState.testGeneration;
    const int generation = appState.testGeneration;
    appState.testingAll = true;
    appState.testTotal = static_cast<int>(appState.nodes.size());
    appState.testDone = 0;
    appState.testPending = appState.testTotal;
    const config::Info info = appState.configInfo;

    for (std::size_t index = 0; index < appState.nodes.size(); ++index) {
        NodeEntry& node = appState.nodes[index];
        node.testing = true;
        const std::string name = node.name;
        async::restart(
            "imclash.delay." + std::to_string(generation) + "." +
                std::to_string(index),
            [info, name]() -> async::Result<int> {
                int delay = -1;
                std::string error;
                if (!mihomo_api::testDelay(info, name, 5000, kTestUrl, delay, error)) {
                    return async::failure<int>(error);
                }
                return async::success(delay);
            },
            [generation, name](const async::Result<int>& result) {
                AppState& appState = state();
                if (generation != appState.testGeneration) return;
                for (NodeEntry& node : appState.nodes) {
                    if (node.name != name) continue;
                    node.testing = false;
                    node.tested = true;
                    node.delay = result.ok ? result.value : -1;
                    break;
                }
                ++appState.testDone;
                --appState.testPending;
                if (appState.testPending <= 0) {
                    appState.testingAll = false;
                    appState.status = "Speed test finished.";
                }
                app::requestUpdate();
            });
    }
    setStatus("Testing " + std::to_string(appState.testTotal) + " nodes...");
}

void testSingleNode(const std::string& name) {
    AppState& appState = state();
    if (!appState.apiReady || appState.testingAll) return;
    NodeEntry* target = nullptr;
    for (NodeEntry& node : appState.nodes) {
        if (node.name == name) {
            target = &node;
            break;
        }
    }
    if (target == nullptr || target->testing) return;
    target->testing = true;
    const int generation = appState.testGeneration;
    const config::Info info = appState.configInfo;
    async::restart(
        "imclash.delay.one." + std::to_string(generation) + "." + name,
        [info, name]() -> async::Result<int> {
            int delay = -1;
            std::string error;
            if (!mihomo_api::testDelay(info, name, 5000, kTestUrl, delay, error)) {
                return async::failure<int>(error);
            }
            return async::success(delay);
        },
        [generation, name](const async::Result<int>& result) {
            AppState& appState = state();
            if (generation != appState.testGeneration) return;
            for (NodeEntry& node : appState.nodes) {
                if (node.name != name) continue;
                node.testing = false;
                node.tested = true;
                node.delay = result.ok ? result.value : -1;
                break;
            }
            app::requestUpdate();
        });
}

void switchNode(const std::string& nodeName) {
    AppState& appState = state();
    if (appState.mainGroup.empty()) {
        setStatus("No main selector group was found.");
        return;
    }
    if (appState.selectingNode) return;

    std::vector<std::string> targets;
    for (const mihomo_api::Group& group : appState.groups) {
        if (util::toLower(group.type) != "selector") continue;
        if (std::find(group.all.begin(), group.all.end(), nodeName) == group.all.end()) {
            continue;
        }
        targets.push_back(group.name);
    }
    if (targets.empty()) {
        setStatus("No Selector group contains " + nodeName + ".");
        return;
    }
    const std::string mainGroup = appState.mainGroup;
    appState.selectingNode = true;
    const int generation = ++appState.selectGeneration;
    const config::Info info = appState.configInfo;
    setStatus("Switching node...");

    async::restart(
        "imclash.select.node",
        [info, targets, nodeName, mainGroup]() -> async::Result<SwitchResult> {
            SwitchResult result;
            result.total = static_cast<int>(targets.size());
            for (const std::string& group : targets) {
                std::string error;
                if (mihomo_api::selectNode(info, group, nodeName, error)) {
                    ++result.switched;
                    if (group == mainGroup) result.mainSwitched = true;
                } else if (result.error.empty()) {
                    result.error = group + ": " + error;
                }
            }
            if (result.switched == 0) {
                return async::failure<SwitchResult>(
                    result.error.empty() ? "No group accepted the node." : result.error);
            }
            return async::success(std::move(result));
        },
        [generation, nodeName](const async::Result<SwitchResult>& result) {
            AppState& appState = state();
            if (generation != appState.selectGeneration) return;
            appState.selectingNode = false;
            if (!result.ok) {
                showError("Cannot switch node", result.error);
                return;
            }
            if (result.value.mainSwitched) {
                appState.mainGroupNow = nodeName;
                appState.currentNode = nodeName;
                appState.nodeScroll = 0.0f;
            }
            std::string status = "Switched " + std::to_string(result.value.switched) +
                                 " group(s) to " + nodeName + ".";
            if (result.value.switched < result.value.total) status += " Some groups failed.";
            setStatus(std::move(status));
        });
}

void toggleProxy() {
    AppState& appState = state();
    if (!appState.proxyAvailable) return;
    appState.proxyUserTouched = true;
    appState.proxyUserWants = !proxyOn();
    updateProxyState();
    requestUpdate();
}

void toggleAutostart() {
    const bool next = !state().autostart;
    if (!imclash::shell::setAutostartEnabled(next)) {
        showError("Startup setting failed",
                  "Cannot update HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run.");
        return;
    }
    state().autostart = next;
    setStatus(next ? "imclash will start with Windows." : "Startup entry removed.");
}

void commitSubscription(const std::string& content, const std::string& url) {
    AppState& appState = state();
    if (!config::looksLikeClashYaml(content)) {
        showError("Invalid subscription",
                  "The content is not a Clash/Mihomo YAML configuration. "
                  "Base64 node lists are not supported.");
        return;
    }

    std::string oldContent;
    std::string ignored;
    const bool hadConfig = fs::readText(appState.configPath, oldContent, ignored);
    const std::string finalContent = config::withUpdateUrl(content, url);
    std::string writeError;
    if (!fs::writeTextAtomic(appState.configPath, finalContent, writeError)) {
        showError("Cannot save config.yaml", writeError);
        return;
    }

    if (appState.coreRunning && appState.apiReady) {
        std::string reloadError;
        if (!mihomo_api::reload(appState.configInfo, appState.configPath, reloadError)) {
            if (hadConfig) {
                std::string restoreError;
                fs::writeTextAtomic(appState.configPath, oldContent, restoreError);
                mihomo_api::reload(appState.configInfo, appState.configPath, restoreError);
            }
            showError("mihomo rejected the new config", reloadError);
            return;
        }
    }

    refreshConfigState();
    appState.nodes.clear();
    appState.nodesLoaded = false;
    appState.nodesError.clear();
    appState.testingAll = false;
    ++appState.testGeneration;
    closeDialog();
    if (appState.coreRunning && appState.apiReady) loadNodes();

    const bool firstConfig = !hadConfig;
    if (firstConfig && !appState.coreRunning && !appState.coreStarting) {
        // The first config may belong to an already running mihomo. Probe the
        // controller first and only start a local core if nothing answers.
        startupProbe();
    }
    appState.busy = false;
    setStatus(firstConfig ? "Subscription saved." : "Subscription updated.");
}

async::Result<std::string> validateSubscription(std::string content,
                                                bool coreAvailable) {
    if (content.size() >= 3 && content.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        content.erase(0, 3);
    }
    if (!config::looksLikeClashYaml(content)) {
        return async::failure<std::string>(
            "The content is not a Clash/Mihomo YAML configuration. "
            "Base64 node lists are not supported.");
    }
    if (!coreAvailable) return async::success(std::move(content));

    const std::string directory = fs::tempDirectory();
    std::string error;
    if (directory.empty() || !fs::ensureDirectory(directory, error)) {
        return async::failure<std::string>("Cannot create a temporary directory.");
    }
    const std::string tempPath = fs::join(
        directory, "subscription-" +
                       std::to_string(static_cast<unsigned long long>(
                           std::chrono::steady_clock::now().time_since_epoch().count())) +
                       ".yaml");
    if (!fs::writeTextAtomic(tempPath, content, error)) {
        return async::failure<std::string>("Cannot write a temporary config: " + error);
    }
    std::string output;
    const bool valid = mihomo_process::testConfig(tempPath, output);
    fs::removeFile(tempPath);
    if (!valid) {
        return async::failure<std::string>(output.empty()
                                               ? "mihomo rejected the configuration."
                                               : output);
    }
    return async::success(std::move(content));
}

void beginDownload(const std::string& url) {
    AppState& appState = state();
    if (appState.busy) return;
    if (!imclash::util::startsWith(url, "http://") &&
        !imclash::util::startsWith(url, "https://")) {
        showError("Invalid URL", "The subscription URL must start with http:// or https://.");
        return;
    }
    const std::string proxy = proxyForDownload();
    const bool coreAvailable = appState.corePresent;
    appState.busy = true;
    appState.busyText = "Downloading subscription...";
    appState.dialog = DialogKind::None;
    appState.menu = MenuKind::None;
    imclash::frame::setTitleBarDragEnabled(true);
    requestUpdate();

    async::restart(
        "imclash.subscription.download",
        [url, proxy, coreAvailable]() -> async::Result<std::string> {
            http::Options options;
            options.userAgent = "clash.meta";
            options.proxy = proxy;
            options.timeoutMs = 30000;
            options.maxBytes = 32u * 1024u * 1024u;
            const http::Response response = http::get(url, options);
            if (!response.ok) {
                return async::failure<std::string>(
                    response.error.empty() ? "Subscription download failed."
                                           : response.error);
            }
            return validateSubscription(response.body, coreAvailable);
        },
        [url](const async::Result<std::string>& result) {
            AppState& appState = state();
            appState.busy = false;
            if (!result.ok) {
                showError("Subscription update failed", result.error);
                return;
            }
            commitSubscription(result.value, url);
        });
}

void beginImport(const std::string& content, const std::string& url) {
    AppState& appState = state();
    if (appState.busy) return;
    const bool coreAvailable = appState.corePresent;
    appState.busy = true;
    appState.busyText = "Checking subscription...";
    appState.dialog = DialogKind::None;
    appState.menu = MenuKind::None;
    imclash::frame::setTitleBarDragEnabled(true);
    requestUpdate();

    async::restart(
        "imclash.subscription.import",
        [content, coreAvailable]() -> async::Result<std::string> {
            return validateSubscription(content, coreAvailable);
        },
        [url](const async::Result<std::string>& result) {
            AppState& appState = state();
            appState.busy = false;
            if (!result.ok) {
                showError("Import failed", result.error);
                return;
            }
            commitSubscription(result.value, url);
        });
}

void openAddUrlDialog() {
    state().addUrl.clear();
    state().dialogError.clear();
    openDialog(DialogKind::AddUrl);
}

void openImportDialog() {
    AppState& appState = state();
    eui::platform::FileDialogOptions options;
    options.prompt = "Import Clash subscription";
    options.allowedExtensions = {"yaml", "yml", "txt"};
    options.filterName = "Clash YAML";
    options.allowMultiple = false;
    const eui::platform::FileDialogResult result = eui::platform::openFileDialog(options);
    if (!result.selected() || result.paths.empty()) return;

    std::string content;
    std::string error;
    if (!fs::readText(result.paths.front(), content, error)) {
        showError("Cannot read file", error);
        return;
    }
    if (!config::looksLikeClashYaml(content)) {
        showError("Invalid subscription",
                  "The selected file is not a Clash/Mihomo YAML configuration.");
        return;
    }
    appState.importPath = result.paths.front();
    appState.importContent = std::move(content);
    appState.importUrl = config::updateUrlFromContent(appState.importContent);
    appState.dialogError.clear();
    openDialog(DialogKind::ImportFile);
}

void executePending() {
    AppState& appState = state();
    const PendingKind kind = appState.pendingKind;
    const std::string url = appState.pendingUrl;
    const std::string content = appState.pendingContent;
    const std::string importUrl = appState.pendingImportUrl;
    appState.pendingKind = PendingKind::None;
    closeDialog();
    if (kind == PendingKind::UrlDownload) {
        beginDownload(url);
    } else if (kind == PendingKind::FileImport) {
        beginImport(content, importUrl);
    }
}

void requestUrlDownload(const std::string& url) {
    AppState& appState = state();
    if (appState.configPresent) {
        appState.pendingKind = PendingKind::UrlDownload;
        appState.pendingUrl = url;
        appState.pendingContent.clear();
        appState.pendingImportUrl.clear();
        openDialog(DialogKind::ConfirmReplace);
    } else {
        closeDialog();
        beginDownload(url);
    }
}

void requestFileImport(const std::string& content, const std::string& url) {
    AppState& appState = state();
    if (appState.configPresent) {
        appState.pendingKind = PendingKind::FileImport;
        appState.pendingContent = content;
        appState.pendingImportUrl = url;
        appState.pendingUrl.clear();
        openDialog(DialogKind::ConfirmReplace);
    } else {
        closeDialog();
        beginImport(content, url);
    }
}

void checkCoreUpdate() {
    AppState& appState = state();
    if (appState.busy) return;
    appState.menu = MenuKind::None;
    imclash::frame::setTitleBarDragEnabled(true);
    const std::string current = appState.coreVersion;
    const std::string proxy = proxyForDownload();
    appState.busy = true;
    appState.busyText = proxy.empty()
        ? "Checking mihomo releases..."
        : "Checking mihomo releases via proxy...";
    requestUpdate();

    async::restart(
        "imclash.core.check",
        [current, proxy]() -> async::Result<core_update::CheckResult> {
            core_update::CheckResult check = core_update::check(current, proxy);
            if (!check.ok) return async::failure<core_update::CheckResult>(check.error);
            return async::success(std::move(check));
        },
        [](const async::Result<core_update::CheckResult>& result) {
            AppState& appState = state();
            appState.busy = false;
            if (!result.ok) {
                showError("mihomo update check failed", result.error);
                return;
            }
            appState.coreCheck = result.value;
            appState.coreCheckValid = true;
            appState.dialog = DialogKind::CoreUpdate;
            appState.menu = MenuKind::None;
            imclash::frame::setTitleBarDragEnabled(false);
            app::requestUpdate();
        });
}

void downloadCore() {
    AppState& appState = state();
    if (appState.busy || !appState.coreCheckValid ||
        appState.coreCheck.downloadUrl.empty()) {
        return;
    }
    const bool wasRunning = appState.coreRunning;
    const bool configAvailable = appState.configPresent;
    const std::string configDirectory = appState.configDir;
    const std::string proxy = proxyForDownload();
    const std::string url = appState.coreCheck.downloadUrl;
    const std::string latest = appState.coreCheck.latest;

    appState.busy = true;
    appState.busyText = "Updating mihomo...";
    appState.coreUpdateInFlight = true;
    appState.coreUpdateStartedAt = std::chrono::steady_clock::now();
    const int updateGeneration = ++appState.coreUpdateGeneration;
    appState.dialog = DialogKind::None;
    appState.menu = MenuKind::None;
    imclash::frame::setTitleBarDragEnabled(true);
    requestUpdate();

    async::restart(
        "imclash.core.download",
        [url, latest, proxy, configAvailable, configDirectory]()
            -> async::Result<core_update::DownloadResult> {
            core_update::DownloadResult result =
                core_update::download(url, latest, proxy, {});
            if (!result.ok) {
                return async::failure<core_update::DownloadResult>(result.error);
            }
            if (configAvailable) {
                std::string output;
                if (!mihomo_process::runCapture(
                        result.executablePath,
                        "-t -d \"" + configDirectory + "\"", 20000, output)) {
                    fs::removeTree(result.tempDirectory);
                    return async::failure<core_update::DownloadResult>(
                        "The new mihomo rejected the current config.yaml.\n" +
                        (output.empty() ? std::string("No output.") : output));
                }
            }
            return async::success(std::move(result));
        },
        [wasRunning, updateGeneration](
            const async::Result<core_update::DownloadResult>& result) {
            AppState& appState = state();
            if (updateGeneration != appState.coreUpdateGeneration) return;
            appState.coreUpdateInFlight = false;
            appState.busy = false;
            if (!result.ok) {
                showError("mihomo update failed", result.error);
                return;
            }

            // Download first so a proxy provided by the running core can be
            // used, then stop the core, install and start it again.
            std::string stopError;
            if (!mihomo_process::stopRunning(stopError)) {
                fs::removeTree(result.value.tempDirectory);
                showError("Cannot stop mihomo", stopError);
                return;
            }
            resetCoreDerivedState();

            std::string installError;
            if (!core_update::install(result.value.executablePath, installError)) {
                fs::removeTree(result.value.tempDirectory);
                showError("mihomo install failed", installError);
                if (wasRunning) startCore();
                return;
            }
            fs::removeTree(result.value.tempDirectory);
            appState.corePresent = true;
            appState.coreVersion = result.value.version;
            appState.coreCheckValid = false;
            if (appState.configPresent) {
                startCore();
            } else {
                setStatus("mihomo updated to " + result.value.version + ".");
            }
        });
}

void toggleSort() {
    state().sortBySpeed = !state().sortBySpeed;
    state().nodeScroll = 0.0f;
    requestUpdate();
}

// --- UI helpers ---------------------------------------------------------------

components::InputStyle inputStyle() {
    components::InputStyle style;
    style.background = kPanel;
    style.focused = kPanel;
    style.border = kBorderStrong;
    style.focusBorder = kAccent;
    style.text = kTextPrimary;
    style.placeholder = kTextSecondary;
    style.cursor = kAccent;
    style.radius = 4.0f;
    return style;
}

void composeClippedText(eui::Ui& ui, const std::string& id, float x, float y,
                        float width, float height, const std::string& text,
                        const eui::Color& color, float fontSize,
                        eui::HorizontalAlign align = eui::HorizontalAlign::Left) {
    ui.stack(id + ".clip").position(x, y).size(width, height).clip().content([&] {
        ui.text(id + ".text")
            .position(0.0f, 0.0f)
            .size(width, height)
            .text(text)
            .color(color)
            .fontSize(fontSize)
            .horizontalAlign(align)
            .verticalAlign(eui::VerticalAlign::Center)
            .build();
    }).build();
}

void composeTextButton(eui::Ui& ui, const std::string& id, float x, float y,
                       float width, float height, const std::string& label,
                       bool primary, bool enabled,
                       const std::function<void()>& onTap) {
    const eui::Color base = primary ? kAccent : kChipIdle;
    const eui::Color hover = primary ? kAccentHover : kChipHover;
    const eui::Color pressed = primary ? kAccentPressed : kChipPressed;
    ui.stack(id).position(x, y).size(width, height).content([&] {
        ui.rect(id + ".bg").size(width, height)
            .color(base)
            .radius(4.0f)
            .border(primary ? 0.0f : 1.0f, primary ? kAccent : kBorderStrong)
            .states(enabled ? base : eui::Color{0.0f, 0.0f, 0.0f, 0.0f},
                    enabled ? hover : eui::Color{0.0f, 0.0f, 0.0f, 0.0f},
                    enabled ? pressed : eui::Color{0.0f, 0.0f, 0.0f, 0.0f})
            .build();
        ui.text(id + ".label").size(width, height).text(label)
            .color(primary ? (enabled ? kWhite : eui::Color{1.0f, 1.0f, 1.0f, 0.38f})
                          : (enabled ? kTextPrimary : kTextSecondary))
            .fontSize(12.0f)
            .horizontalAlign(eui::HorizontalAlign::Center)
            .verticalAlign(eui::VerticalAlign::Center)
            .build();
        ui.rect(id + ".hit").size(width, height)
            .color({0.0f, 0.0f, 0.0f, 0.0f})
            .onClick(enabled ? onTap : std::function<void()>{})
            .cursor(enabled ? eui::CursorShape::Hand : eui::CursorShape::Arrow)
            .build();
    }).build();
}

void composeMenuButton(eui::Ui& ui, const std::string& id, float x, float width,
                       const std::string& label, bool active,
                       const std::function<void()>& onTap) {
    const float y = (kTitleBarHeight - kMenuButtonHeight) * 0.5f;
    ui.stack(id).position(x, y).size(width, kMenuButtonHeight).content([&] {
        const eui::Color base = active ? kChipHover : kChipIdle;
        ui.rect(id + ".bg").size(width, kMenuButtonHeight)
            .color(base).radius(4.0f)
            .states(base, kChipHover, kChipPressed).build();
        ui.text(id + ".label").position(10.0f, 0.0f)
            .size(width - 14.0f, kMenuButtonHeight).text(label)
            .color(kTextPrimary).fontSize(12.0f)
            .verticalAlign(eui::VerticalAlign::Center).build();
        ui.rect(id + ".hit").size(width, kMenuButtonHeight)
            .color({0.0f, 0.0f, 0.0f, 0.0f})
            .onClick(onTap)
            .cursor(eui::CursorShape::Hand)
            .build();
    }).build();
}

void composeToggleChip(eui::Ui& ui, const std::string& id, float x, float width,
                       const std::string& label, bool on, bool enabled,
                       const std::function<void()>& onTap) {
    const float y = (kTitleBarHeight - kMenuButtonHeight) * 0.5f;
    const eui::Color base = on ? kAccent : kChipIdle;
    const eui::Color hover = on ? kAccentHover : kChipHover;
    const eui::Color pressed = on ? kAccentPressed : kChipPressed;
    ui.stack(id).position(x, y).size(width, kMenuButtonHeight).content([&] {
        ui.rect(id + ".bg").size(width, kMenuButtonHeight)
            .color(base).radius(4.0f)
            .states(enabled ? base : eui::Color{0.0f, 0.0f, 0.0f, 0.0f},
                    enabled ? hover : eui::Color{0.0f, 0.0f, 0.0f, 0.0f},
                    enabled ? pressed : eui::Color{0.0f, 0.0f, 0.0f, 0.0f})
            .build();
        ui.text(id + ".label").size(width, kMenuButtonHeight).text(label)
            .color(on ? kWhite : (enabled ? kTextSecondary : eui::Color{1.0f, 1.0f, 1.0f, 0.38f}))
            .fontSize(11.0f)
            .horizontalAlign(eui::HorizontalAlign::Center)
            .verticalAlign(eui::VerticalAlign::Center)
            .build();
        ui.rect(id + ".hit").size(width, kMenuButtonHeight)
            .color({0.0f, 0.0f, 0.0f, 0.0f})
            .onClick(enabled ? onTap : std::function<void()>{})
            .cursor(enabled ? eui::CursorShape::Hand : eui::CursorShape::Arrow)
            .build();
    }).build();
}

void composeMinimizeButton(eui::Ui& ui, float x) {
    ui.stack("titlebar.minimize").position(x, 0.0f)
        .size(kMinimizeWidth, kTitleBarHeight).content([&] {
            ui.rect("titlebar.minimize.bg").size(kMinimizeWidth, kTitleBarHeight)
                .color({0.0f, 0.0f, 0.0f, 0.0f})
                .states({0.0f, 0.0f, 0.0f, 0.0f}, kChipHover, kChipPressed)
                .build();
            const float cx = kMinimizeWidth * 0.5f;
            const float cy = kTitleBarHeight * 0.5f;
            ui.rect("titlebar.minimize.glyph").position(cx - 5.0f, cy)
                .size(10.0f, 1.0f).color(kTextSecondary).build();
            ui.rect("titlebar.minimize.hit").size(kMinimizeWidth, kTitleBarHeight)
                .color({0.0f, 0.0f, 0.0f, 0.0f})
                .onClick([] { imclash::frame::hideToTray(); })
                .cursor(eui::CursorShape::Hand)
                .build();
        }).build();
}

void composeTitleBar(eui::Ui& ui, const eui::Screen& screen) {
    ui.stack("titlebar").position(0.0f, 0.0f)
        .size(screen.width, kTitleBarHeight).zIndex(100).content([&] {
            ui.rect("titlebar.bg").size(screen.width, kTitleBarHeight)
                .color(kTitleBar).build();
            ui.rect("titlebar.line").position(0.0f, kTitleBarHeight - 1.0f)
                .size(screen.width, 1.0f).color(kBorder).build();
            ui.rect("titlebar.drag").size(screen.width, kTitleBarHeight)
                .color({0.0f, 0.0f, 0.0f, 0.0f})
                .cursor(eui::CursorShape::Arrow)
                .build();

            const float mihomoX = 8.0f;
            const float subscriptionX = mihomoX + kMihomoButtonWidth + 4.0f;
            composeMenuButton(ui, "titlebar.mihomo", mihomoX, kMihomoButtonWidth,
                              "Mihomo", state().menu == MenuKind::Mihomo, [] {
                                  if (state().menu == MenuKind::Mihomo) {
                                      setMenuOpen(MenuKind::None, 0.0f, 0.0f);
                                  } else {
                                      setMenuOpen(MenuKind::Mihomo, 8.0f,
                                                  kTitleBarHeight + 2.0f);
                                  }
                              });
            composeMenuButton(ui, "titlebar.subscription", subscriptionX,
                              kSubscriptionButtonWidth, "Subscription",
                              state().menu == MenuKind::Subscription, [subscriptionX] {
                                  if (state().menu == MenuKind::Subscription) {
                                      setMenuOpen(MenuKind::None, 0.0f, 0.0f);
                                  } else {
                                      setMenuOpen(MenuKind::Subscription, subscriptionX,
                                                  kTitleBarHeight + 2.0f);
                                  }
                              });

            const float minimizeX = screen.width - kMinimizeWidth;
            const float startupX = minimizeX - 8.0f - kStartupChipWidth;
            const float proxyX = startupX - 6.0f - kProxyChipWidth;
            const std::string proxyLabel = state().proxyAvailable
                ? (state().viaProxy ? "Via proxy: On" : "Via proxy: Off")
                : "Via proxy: N/A";
            composeToggleChip(ui, "titlebar.proxy", proxyX, kProxyChipWidth,
                              proxyLabel, state().viaProxy,
                              state().proxyAvailable, [] { toggleProxy(); });
            composeToggleChip(ui, "titlebar.startup", startupX, kStartupChipWidth,
                              state().autostart ? "Startup: On" : "Startup: Off",
                              state().autostart, true, [] { toggleAutostart(); });
            composeMinimizeButton(ui, minimizeX);
        }).build();
}

void composeEmptyState(eui::Ui& ui, const eui::Screen& screen) {
    const float cardWidth = std::min(560.0f, screen.width - 32.0f);
    const float cardHeight = 268.0f;
    const float x = (screen.width - cardWidth) * 0.5f;
    const float y = kTitleBarHeight +
                    (screen.height - kTitleBarHeight - kStatusBarHeight - cardHeight) * 0.5f;
    ui.stack("empty").position(x, y).size(cardWidth, cardHeight).content([&] {
        ui.rect("empty.bg").size(cardWidth, cardHeight).color(kPanel)
            .radius(kCardRadius).border(1.0f, kBorder).build();
        ui.text("empty.title").position(24.0f, 28.0f)
            .size(cardWidth - 48.0f, 30.0f).text("No subscription")
            .color(kTextPrimary).fontSize(20.0f).build();
        ui.text("empty.body").position(24.0f, 66.0f)
            .size(cardWidth - 48.0f, 44.0f)
            .text("Add a Clash YAML subscription from a URL, or import a local config file.")
            .color(kTextSecondary).fontSize(12.0f).wrap(true).lineHeight(18.0f).build();
        composeTextButton(ui, "empty.add", 24.0f, 126.0f, 132.0f, 32.0f,
                          "Add from URL", true, !state().busy, [] { openAddUrlDialog(); });
        composeTextButton(ui, "empty.import", 164.0f, 126.0f, 120.0f, 32.0f,
                          "Import file", false, !state().busy, [] { openImportDialog(); });

        if (!state().corePresent && !state().coreRunning) {
            ui.rect("empty.core.line").position(24.0f, 184.0f)
                .size(cardWidth - 48.0f, 1.0f).color(kBorder).build();
            ui.text("empty.core.text").position(24.0f, 196.0f)
                .size(cardWidth - 176.0f, 28.0f)
                .text("mihomo.exe was not found in the mihomo directory.")
                .color(kWarning).fontSize(11.0f).build();
            composeTextButton(ui, "empty.core.download", cardWidth - 150.0f, 196.0f,
                              126.0f, 28.0f, "Download mihomo", false, !state().busy,
                              [] { checkCoreUpdate(); });
        }
    }).build();
}

void composeSubscriptionCard(eui::Ui& ui, const eui::Screen& screen, float x,
                             float y, float width, float height) {
    ui.rect("sub.card").position(x, y).size(width, height).color(kPanel)
        .radius(kCardRadius).border(1.0f, kBorder).build();

    ui.text("sub.label").position(x + 16.0f, y + 14.0f)
        .size(width - 32.0f, 16.0f).text("CURRENT SUBSCRIPTION")
        .color(kTextSecondary).fontSize(10.0f).build();

    const std::string title = state().sourceUrl.empty()
        ? "Local subscription file"
        : state().sourceUrl;
    composeClippedText(ui, "sub.title", x + 16.0f, y + 34.0f,
                       width - 280.0f, 26.0f, title, kTextPrimary, 15.0f);

    const std::string nodeCount = state().nodesLoaded
        ? std::to_string(state().nodes.size()) + " nodes"
        : (state().apiReady ? "nodes loading" : "nodes unavailable");
    const std::string meta = "Updated " + imclash::util::formatRelativeTime(state().configModified) +
                             "  ·  " + nodeCount + "  ·  " +
                             imclash::util::formatBytes(state().configBytes);
    ui.text("sub.meta").position(x + 16.0f, y + 62.0f)
        .size(width - 32.0f, 18.0f).text(meta)
        .color(kTextSecondary).fontSize(11.0f).build();

    composeTextButton(ui, "sub.openconfig", x + width - 236.0f, y + 30.0f,
                      100.0f, 30.0f, "Open config", false,
                      state().configPresent && !state().busy,
                      [] { imclash::shell::openInEditor(state().configPath); });
    composeTextButton(ui, "sub.update", x + width - 126.0f, y + 30.0f,
                      110.0f, 30.0f, "Update now", true,
                      state().configPresent && !state().sourceUrl.empty() && !state().busy,
                      [] { beginDownload(state().sourceUrl); });

    const float statusY = y + 96.0f;
    const eui::Color dot = state().coreRunning
        ? (state().apiReady ? kSuccess : kWarning)
        : (state().corePresent ? kTextSecondary : kWarning);
    ui.rect("sub.core.dot").position(x + 16.0f, statusY + 5.0f)
        .size(6.0f, 6.0f).radius(3.0f).color(dot).build();
    std::string coreText;
    if (state().coreStarting) {
        coreText = "Mihomo " + state().coreVersion + " · Starting...";
    } else if (state().coreRunning) {
        coreText = "Mihomo " + state().coreVersion + " · " +
                   (state().apiReady ? "Running" : "API unavailable") +
                   (state().coreOwned ? "" : " (external)");
        const std::string proxy = config::proxyAddress(state().configInfo);
        if (!proxy.empty()) coreText += " · " + proxy;
    } else if (!state().corePresent) {
        coreText = "mihomo.exe not found";
    } else {
        coreText = "Mihomo " + (state().coreVersion.empty() ? "unknown" : state().coreVersion) +
                   " · Stopped";
    }
    composeClippedText(ui, "sub.core", x + 30.0f, statusY - 2.0f,
                       width - 190.0f, 22.0f, coreText, kTextSecondary, 11.0f);

    if (!state().corePresent && !state().coreRunning) {
        composeTextButton(ui, "sub.download", x + width - 152.0f, statusY - 4.0f,
                          136.0f, 28.0f, "Download mihomo", false, !state().busy,
                          [] { checkCoreUpdate(); });
    } else if (state().corePresent && state().coreCheckValid &&
               state().coreCheck.updateAvailable) {
        composeTextButton(ui, "sub.coreupdate", x + width - 152.0f, statusY - 4.0f,
                          136.0f, 28.0f, "Update available", false, !state().busy,
                          [] { checkCoreUpdate(); });
    }

    if (!state().configInfo.validClashYaml) {
        ui.text("sub.invalid").position(x + 16.0f, y + height - 26.0f)
            .size(width - 32.0f, 18.0f)
            .text("The current config.yaml does not look like a Clash configuration.")
            .color(kWarning).fontSize(11.0f).build();
    } else if (!state().corePresent && state().coreRunning) {
        ui.text("sub.external").position(x + 16.0f, y + height - 26.0f)
            .size(width - 32.0f, 18.0f)
            .text("Using an external mihomo instance; no local mihomo.exe was found.")
            .color(kTextSecondary).fontSize(10.5f).build();
    } else {
        composeClippedText(ui, "sub.path", x + 16.0f, y + height - 26.0f,
                           width - 32.0f, 18.0f, state().configPath,
                           kTextSecondary, 10.0f);
    }
}

void composeNodeRow(eui::Ui& ui, NodeEntry* node, const std::string& id,
                    float y, float width) {
    const bool current = !state().currentNode.empty() &&
                         state().currentNode == node->name;
    const bool hoverable = state().apiReady && !state().testingAll && !node->testing;
    const bool canTest = state().apiReady && !state().testingAll && !node->testing;
    const auto scrollList = [](const core::ScrollEvent& event) {
        if (event.y == 0.0) return;
        AppState& appState = state();
        appState.nodeScroll = std::clamp(
            appState.nodeScroll - static_cast<float>(event.y) * 48.0f,
            0.0f, std::max(0.0f, static_cast<float>(appState.nodes.size()) *
                                            kRowHeight - appState.listH));
        app::requestUpdate();
    };

    ui.stack(id).position(0.0f, y).size(width, kRowHeight).content([&] {
        const eui::Color selected{0.043f, 0.231f, 0.376f, 1.0f};
        const eui::Color base = current ? selected : eui::Color{0.0f, 0.0f, 0.0f, 0.0f};
        const eui::Color hover = current
            ? selected
            : (hoverable ? kRowHover : eui::Color{0.0f, 0.0f, 0.0f, 0.0f});
        ui.rect(id + ".bg").size(width, kRowHeight)
            .color(base).states(base, hover, hover).build();
        ui.rect(id + ".line").position(0.0f, kRowHeight - 1.0f)
            .size(width, 1.0f).color({0.169f, 0.169f, 0.169f, 0.45f}).build();
        if (current) {
            ui.rect(id + ".dot").position(4.0f, kRowHeight * 0.5f - 2.0f)
                .size(4.0f, 4.0f).radius(2.0f).color(kAccent).build();
        }

        const float nameWidth = std::max(100.0f, width - 254.0f);
        composeClippedText(ui, id + ".name", 12.0f, 0.0f, nameWidth, kRowHeight,
                           node->name, current ? kWhite : kTextPrimary, 12.0f);
        composeClippedText(ui, id + ".type", width - 234.0f, 0.0f, 100.0f,
                           kRowHeight, node->type, kTextSecondary, 11.0f);

        std::string latency;
        eui::Color latencyColor = kTextSecondary;
        if (node->testing) {
            latency = "testing...";
            latencyColor = kWarning;
        } else if (node->delay > 0) {
            latency = std::to_string(node->delay) + " ms";
            latencyColor = node->delay < 200 ? kSuccess
                : (node->delay < 500 ? kWarning : kError);
        } else if (node->tested) {
            latency = "timeout";
            latencyColor = kError;
        } else {
            latency = "—";
        }
        composeClippedText(ui, id + ".latency", width - 128.0f, 0.0f, 60.0f,
                           kRowHeight, latency, latencyColor, 11.0f,
                           eui::HorizontalAlign::Right);

        ui.rect(id + ".hit").size(width - 58.0f, kRowHeight)
            .color({0.0f, 0.0f, 0.0f, 0.0f})
            .onClick([name = node->name] {
                if (!state().selectingNode) switchNode(name);
            })
            .onScroll(scrollList)
            .cursor(eui::CursorShape::Hand)
            .build();

        ui.rect(id + ".test.bg").position(width - 58.0f, 0.0f)
            .size(58.0f, kRowHeight)
            .color({0.0f, 0.0f, 0.0f, 0.0f})
            .states({0.0f, 0.0f, 0.0f, 0.0f},
                    canTest ? kRowHover : eui::Color{0.0f, 0.0f, 0.0f, 0.0f},
                    canTest ? kChipPressed : eui::Color{0.0f, 0.0f, 0.0f, 0.0f})
            .build();
        ui.text(id + ".test").position(width - 58.0f, 0.0f)
            .size(58.0f, kRowHeight).text("Test")
            .color(canTest ? kTextSecondary : eui::Color{1.0f, 1.0f, 1.0f, 0.24f})
            .fontSize(10.0f)
            .horizontalAlign(eui::HorizontalAlign::Center)
            .verticalAlign(eui::VerticalAlign::Center).build();
        ui.rect(id + ".test.hit").position(width - 58.0f, 0.0f)
            .size(58.0f, kRowHeight)
            .color({0.0f, 0.0f, 0.0f, 0.0f})
            .onClick(canTest ? std::function<void()>([name = node->name] {
                         testSingleNode(name);
                     })
                             : std::function<void()>{})
            .onScroll(scrollList)
            .cursor(canTest ? eui::CursorShape::Hand : eui::CursorShape::Arrow)
            .build();
    }).build();
}

void composeNodes(eui::Ui& ui, const eui::Screen& screen, float x, float y,
                  float width, float height) {
    ui.text("nodes.title").position(x, y).size(240.0f, 24.0f)
        .text("Nodes (" + std::to_string(state().nodes.size()) + ")")
        .color(kTextPrimary).fontSize(13.0f)
        .verticalAlign(eui::VerticalAlign::Center).build();

    const float buttonY = y - 2.0f;
    composeTextButton(ui, "nodes.test", x + width - 178.0f, buttonY, 78.0f, 26.0f,
                      state().testingAll ? "Testing" : "Test all", false,
                      state().apiReady && !state().nodes.empty() &&
                          !state().nodesLoading && !state().testingAll && !state().busy,
                      [] { testAllNodes(); });
    composeTextButton(ui, "nodes.default", x + width - 94.0f, buttonY, 46.0f, 26.0f,
                      "Default", !state().sortBySpeed, true, [] {
                          if (state().sortBySpeed) toggleSort();
                      });
    composeTextButton(ui, "nodes.fastest", x + width - 44.0f, buttonY, 44.0f, 26.0f,
                      "Fastest", state().sortBySpeed, true, [] {
                          if (!state().sortBySpeed) toggleSort();
                      });

    std::string groupLine;
    if (state().mainGroup.empty()) {
        groupLine = "Main group: not found";
    } else {
        const std::string displayCurrent = !state().currentNode.empty()
            ? state().currentNode
            : state().mainGroupNow;
        groupLine = "Main group: " + state().mainGroup + "  \xE2\x86\x92  " +
                    (state().selectingNode
                         ? "Switching..."
                         : (displayCurrent.empty() ? "\xE2\x80\x94"
                                                   : displayCurrent));
    }
    composeClippedText(ui, "nodes.group", x, y + 24.0f, width, 18.0f,
                       groupLine, kTextSecondary, 10.5f);

    const float cardY = y + 48.0f;
    const float cardH = height - 48.0f;
    ui.rect("nodes.card").position(x, cardY).size(width, cardH).color(kPanel)
        .radius(kCardRadius).border(1.0f, kBorder).build();
    ui.text("nodes.header.name").position(x + 12.0f, cardY + 8.0f)
        .size(width - 254.0f, 16.0f).text("Name")
        .color(kTextSecondary).fontSize(10.0f).build();
    ui.text("nodes.header.type").position(x + width - 234.0f, cardY + 8.0f)
        .size(100.0f, 16.0f).text("Type")
        .color(kTextSecondary).fontSize(10.0f).build();
    ui.text("nodes.header.latency").position(x + width - 128.0f, cardY + 8.0f)
        .size(60.0f, 16.0f).text("Latency")
        .color(kTextSecondary).fontSize(10.0f)
        .horizontalAlign(eui::HorizontalAlign::Right).build();
    ui.text("nodes.header.test").position(x + width - 58.0f, cardY + 8.0f)
        .size(58.0f, 16.0f).text("Test")
        .color(kTextSecondary).fontSize(10.0f)
        .horizontalAlign(eui::HorizontalAlign::Center).build();
    ui.rect("nodes.header.line").position(x, cardY + 28.0f)
        .size(width, 1.0f).color(kBorder).build();

    const float listTop = cardY + 29.0f;
    const float listHeight = std::max(0.0f, cardH - 29.0f);
    state().listX = x;
    state().listY = listTop;
    state().listW = width;
    state().listH = listHeight;

    if (!state().apiReady) {
        const std::string message = !state().corePresent && !state().coreRunning
            ? "mihomo.exe is not installed."
            : (!state().coreRunning ? "Start mihomo to load nodes."
                                    : "Waiting for the mihomo API...");
        ui.text("nodes.empty").position(x, listTop + listHeight * 0.5f - 22.0f)
            .size(width, 20.0f).text(message)
            .color(kTextSecondary).fontSize(12.0f)
            .horizontalAlign(eui::HorizontalAlign::Center).build();
        if (!state().coreRunning) {
            composeTextButton(ui, "nodes.start",
                              x + width * 0.5f - 58.0f, listTop + listHeight * 0.5f + 8.0f,
                              116.0f, 28.0f,
                              !state().corePresent ? "Download mihomo" : "Start mihomo",
                              false, !state().busy && state().configPresent, [] {
                                  if (!state().corePresent) {
                                      checkCoreUpdate();
                                  } else {
                                      startCore();
                                  }
                              });
        }
        return;
    }

    if (state().nodesLoading) {
        ui.text("nodes.loading").position(x, listTop + listHeight * 0.5f - 10.0f)
            .size(width, 20.0f).text("Loading nodes...")
            .color(kTextSecondary).fontSize(12.0f)
            .horizontalAlign(eui::HorizontalAlign::Center).build();
        return;
    }
    if (!state().nodesError.empty()) {
        ui.text("nodes.error").position(x + 16.0f, listTop + 16.0f)
            .size(width - 32.0f, 40.0f).text(state().nodesError)
            .color(kError).fontSize(11.0f).wrap(true).build();
        return;
    }
    if (state().nodes.empty()) {
        ui.text("nodes.none").position(x, listTop + listHeight * 0.5f - 10.0f)
            .size(width, 20.0f).text("No proxy nodes were returned.")
            .color(kTextSecondary).fontSize(12.0f)
            .horizontalAlign(eui::HorizontalAlign::Center).build();
        return;
    }

    std::vector<NodeEntry*> order = sortedNodes();
    const float contentHeight = static_cast<float>(order.size()) * kRowHeight;
    const float maxScroll = std::max(0.0f, contentHeight - listHeight);
    state().nodeScroll = std::clamp(state().nodeScroll, 0.0f, maxScroll);

    ui.stack("nodes.list").position(x, listTop).size(width, listHeight).clip().content([&] {
        ui.rect("nodes.wheel").size(width, std::max(listHeight, 1.0f))
            .color({0.0f, 0.0f, 0.0f, 0.0f})
            .onScroll([maxScroll](const core::ScrollEvent& event) {
                if (event.y == 0.0) return;
                AppState& appState = state();
                appState.nodeScroll = std::clamp(
                    appState.nodeScroll - static_cast<float>(event.y) * 48.0f,
                    0.0f, maxScroll);
                app::requestUpdate();
            })
            .cursor(eui::CursorShape::Arrow)
            .build();

        const int first = std::max(0, static_cast<int>(state().nodeScroll / kRowHeight));
        const int visible = static_cast<int>(listHeight / kRowHeight) + 2;
        const int last = std::min(static_cast<int>(order.size()), first + visible);
        for (int index = first; index < last; ++index) {
            NodeEntry* node = order[static_cast<std::size_t>(index)];
            const float rowY = static_cast<float>(index) * kRowHeight - state().nodeScroll;
            composeNodeRow(ui, node, "node" + std::to_string(index), rowY, width);
        }
    }).build();

    if (maxScroll > 0.0f) {
        const float thumbHeight = std::max(24.0f, listHeight * (listHeight / contentHeight));
        const float progress = maxScroll > 0.0f ? state().nodeScroll / maxScroll : 0.0f;
        const float thumbY = listTop + (listHeight - thumbHeight) * progress;
        ui.rect("nodes.scrollbar").position(x + width - 7.0f, thumbY)
            .size(4.0f, thumbHeight).radius(2.0f)
            .color({1.0f, 1.0f, 1.0f, 0.22f}).build();
    }
}

void composeSubscriptionView(eui::Ui& ui, const eui::Screen& screen) {
    const float contentWidth = screen.width - kMargin * 2.0f;
    const float cardHeight = 164.0f;
    composeSubscriptionCard(ui, screen, kMargin, kTitleBarHeight + kMargin,
                            contentWidth, cardHeight);
    const float nodesTop = kTitleBarHeight + kMargin + cardHeight + kMargin;
    const float nodesHeight = screen.height - kStatusBarHeight - nodesTop - kMargin;
    composeNodes(ui, screen, kMargin, nodesTop, contentWidth,
                 std::max(80.0f, nodesHeight));
}

void composeStatusBar(eui::Ui& ui, const eui::Screen& screen) {
    const float y = screen.height - kStatusBarHeight;
    ui.rect("status.bg").position(0.0f, y).size(screen.width, kStatusBarHeight)
        .color(kTitleBar).build();
    ui.rect("status.line").position(0.0f, y).size(screen.width, 1.0f)
        .color(kBorder).build();
    const std::string text = state().busy
        ? state().busyText
        : (!state().status.empty() ? state().status
                                   : ("Config: " + state().configDir));
    composeClippedText(ui, "status.text", 14.0f, y + 1.0f,
                       screen.width - 28.0f, kStatusBarHeight - 1.0f, text,
                       state().busy ? kAccent : kTextSecondary, 10.5f);
}

struct MenuItem {
    std::string label;
    bool enabled = true;
    bool separator = false;
    std::function<void()> action;
};

void composeMenu(eui::Ui& ui, const eui::Screen& screen) {
    if (state().menu == MenuKind::None) return;

    std::vector<MenuItem> items;
    if (state().menu == MenuKind::Mihomo) {
        if (!state().corePresent && !state().coreRunning) {
            items.push_back({"Download mihomo...", !state().busy, false,
                             [] { checkCoreUpdate(); }});
        } else if (state().corePresent) {
            items.push_back({"Start", !state().coreRunning && state().configPresent &&
                                          !state().busy, false,
                             [] { startCore(); }});
            items.push_back({"Stop", state().coreRunning && state().coreOwned &&
                                          !state().busy, false,
                             [] { stopCore(); }});
            items.push_back({"Restart", state().coreRunning && state().coreOwned &&
                                             !state().busy, false,
                             [] { restartCore(); }});
            items.push_back({"Check for updates...", !state().busy, false,
                             [] { checkCoreUpdate(); }});
        } else {
            items.push_back({"Running externally", false, false, {}});
        }
        items.push_back({"", true, true, {}});
        items.push_back({"Open core folder", true, false,
                         [] { imclash::shell::openFolder(state().configDir); }});
    } else {
        items.push_back({"Add from URL...", !state().busy, false,
                         [] { openAddUrlDialog(); }});
        items.push_back({"Import file...", !state().busy, false,
                         [] { openImportDialog(); }});
        items.push_back({"Update now", state().configPresent && !state().sourceUrl.empty() &&
                                          !state().busy, false,
                         [] { beginDownload(state().sourceUrl); }});
        items.push_back({"", true, true, {}});
        items.push_back({"Open config file", state().configPresent, false,
                         [] { imclash::shell::openInEditor(state().configPath); }});
        items.push_back({"Open data folder", true, false,
                         [] { imclash::shell::openFolder(state().configDir); }});
    }

    constexpr float itemHeight = 26.0f;
    constexpr float separatorHeight = 9.0f;
    constexpr float menuWidth = 220.0f;
    float menuHeight = 8.0f;
    for (const MenuItem& item : items) {
        menuHeight += item.separator ? separatorHeight : itemHeight;
    }
    const float x = std::clamp(state().menuX, 8.0f,
                               std::max(8.0f, screen.width - menuWidth - 8.0f));
    const float y = std::clamp(state().menuY, 8.0f,
                               std::max(8.0f, screen.height - menuHeight - 8.0f));

    ui.rect("menu.backdrop").size(screen.width, screen.height).zIndex(300)
        .color({0.0f, 0.0f, 0.0f, 0.0f})
        .onClick([] { setMenuOpen(MenuKind::None, 0.0f, 0.0f); })
        .cursor(eui::CursorShape::Arrow).build();

    ui.stack("menu").position(x, y).size(menuWidth, menuHeight).zIndex(400).content([&] {
        ui.rect("menu.bg").size(menuWidth, menuHeight).color(kPanelRaised)
            .radius(5.0f).border(1.0f, kBorderStrong)
            .shadow(16.0f, 0.0f, 6.0f, {0.0f, 0.0f, 0.0f, 0.5f}).build();
        float itemY = 4.0f;
        for (std::size_t index = 0; index < items.size(); ++index) {
            const MenuItem& item = items[index];
            if (item.separator) {
                ui.rect("menu.sep" + std::to_string(index))
                    .position(8.0f, itemY + 4.0f)
                    .size(menuWidth - 16.0f, 1.0f).color(kBorder).build();
                itemY += separatorHeight;
                continue;
            }
            const std::string itemId = "menu.item" + std::to_string(index);
            ui.stack(itemId).position(4.0f, itemY)
                .size(menuWidth - 8.0f, itemHeight).content([&, item, itemId] {
                    ui.rect(itemId + ".bg").size(menuWidth - 8.0f, itemHeight)
                        .color({0.0f, 0.0f, 0.0f, 0.0f}).radius(3.0f)
                        .states({0.0f, 0.0f, 0.0f, 0.0f},
                                item.enabled ? kChipHover : eui::Color{0.0f, 0.0f, 0.0f, 0.0f},
                                item.enabled ? kChipPressed : eui::Color{0.0f, 0.0f, 0.0f, 0.0f})
                        .build();
                    ui.text(itemId + ".label").position(8.0f, 0.0f)
                        .size(menuWidth - 24.0f, itemHeight).text(item.label)
                        .color(item.enabled ? kTextPrimary : kTextSecondary)
                        .fontSize(12.0f)
                        .verticalAlign(eui::VerticalAlign::Center).build();
                    if (item.enabled) {
                        ui.rect(itemId + ".hit").size(menuWidth - 8.0f, itemHeight)
                            .color({0.0f, 0.0f, 0.0f, 0.0f})
                            .onClick([item] {
                                setMenuOpen(MenuKind::None, 0.0f, 0.0f);
                                if (item.action) item.action();
                            })
                            .cursor(eui::CursorShape::Hand)
                            .build();
                    }
                }).build();
            itemY += itemHeight;
        }
    }).build();
}

void composeDialog(eui::Ui& ui, const eui::Screen& screen) {
    if (state().dialog == DialogKind::None) return;

    float width = 480.0f;
    float height = 260.0f;
    if (state().dialog == DialogKind::ImportFile) height = 300.0f;
    if (state().dialog == DialogKind::ConfirmReplace) height = 218.0f;
    if (state().dialog == DialogKind::CoreUpdate) height = 320.0f;
    if (state().dialog == DialogKind::Error) height = 300.0f;
    width = std::min(width, screen.width - 32.0f);
    height = std::min(height, screen.height - 32.0f);
    const float x = (screen.width - width) * 0.5f;
    const float y = (screen.height - height) * 0.5f;
    const float innerX = 24.0f;
    const float innerW = width - 48.0f;
    const float buttonsY = height - 48.0f;

    ui.rect("dialog.backdrop").size(screen.width, screen.height).zIndex(500)
        .color({0.0f, 0.0f, 0.0f, 0.55f}).build();
    ui.stack("dialog").position(x, y).size(width, height).zIndex(501).content([&] {
        ui.rect("dialog.bg").size(width, height).color(kPanelRaised)
            .radius(kCardRadius).border(1.0f, kBorderStrong)
            .shadow(24.0f, 0.0f, 10.0f, {0.0f, 0.0f, 0.0f, 0.55f}).build();

        if (state().dialog == DialogKind::AddUrl) {
            ui.text("dialog.title").position(innerX, 18.0f)
                .size(innerW, 24.0f).text("Add subscription")
                .color(kTextPrimary).fontSize(16.0f).build();
            ui.text("dialog.body").position(innerX, 48.0f)
                .size(innerW, 34.0f)
                .text("Download a Clash YAML subscription from a URL. "
                      "The URL is written to the first line of config.yaml.")
                .color(kTextSecondary).fontSize(11.0f).wrap(true).lineHeight(16.0f).build();
            ui.text("dialog.label").position(innerX, 88.0f)
                .size(innerW, 16.0f).text("Subscription URL")
                .color(kTextSecondary).fontSize(10.0f).build();
            components::input(ui, "dialog.url")
                .position(innerX, 106.0f).size(innerW, 34.0f)
                .value(state().addUrl)
                .placeholder("https://example.com/sub?token=...")
                .fontSize(12.0f)
                .inset(9.0f)
                .style(inputStyle())
                .onChange([](const std::string& value) { state().addUrl = value; })
                .build();
            if (state().configPresent) {
                ui.text("dialog.replace.note").position(innerX, 148.0f)
                    .size(innerW, 18.0f)
                    .text("This will replace the current config.yaml.")
                    .color(kWarning).fontSize(10.5f).build();
            }
            if (!state().dialogError.empty()) {
                ui.text("dialog.error").position(innerX, 168.0f)
                    .size(innerW, 18.0f).text(state().dialogError)
                    .color(kError).fontSize(10.5f).build();
            }
            composeTextButton(ui, "dialog.cancel", width - 204.0f, buttonsY,
                              90.0f, 30.0f, "Cancel", false, true,
                              [] { closeDialog(); });
            composeTextButton(ui, "dialog.add", width - 106.0f, buttonsY,
                              82.0f, 30.0f, "Add", true, !state().busy, [] {
                                  const std::string url = imclash::util::trim(state().addUrl);
                                  if (!imclash::util::startsWith(url, "http://") &&
                                      !imclash::util::startsWith(url, "https://")) {
                                      state().dialogError = "Enter an http:// or https:// URL.";
                                      requestUpdate();
                                      return;
                                  }
                                  requestUrlDownload(url);
                              });
        } else if (state().dialog == DialogKind::ImportFile) {
            ui.text("dialog.title").position(innerX, 18.0f)
                .size(innerW, 24.0f).text("Import subscription file")
                .color(kTextPrimary).fontSize(16.0f).build();
            ui.text("dialog.body").position(innerX, 48.0f)
                .size(innerW, 34.0f)
                .text("The file will become config.yaml. If an update URL is set, "
                      "it is stored as the first line.")
                .color(kTextSecondary).fontSize(11.0f).wrap(true).lineHeight(16.0f).build();
            composeClippedText(ui, "dialog.file", innerX, 86.0f, innerW, 18.0f,
                               state().importPath, kTextPrimary, 11.0f);
            ui.text("dialog.label").position(innerX, 112.0f)
                .size(innerW, 16.0f).text("Update URL (optional)")
                .color(kTextSecondary).fontSize(10.0f).build();
            components::input(ui, "dialog.importUrl")
                .position(innerX, 130.0f).size(innerW, 34.0f)
                .value(state().importUrl)
                .placeholder("https://example.com/sub?token=...")
                .fontSize(12.0f)
                .inset(9.0f)
                .style(inputStyle())
                .onChange([](const std::string& value) { state().importUrl = value; })
                .build();
            if (state().configPresent) {
                ui.text("dialog.replace.note").position(innerX, 172.0f)
                    .size(innerW, 18.0f)
                    .text("This will replace the current config.yaml.")
                    .color(kWarning).fontSize(10.5f).build();
            }
            if (!state().dialogError.empty()) {
                ui.text("dialog.error").position(innerX, 192.0f)
                    .size(innerW, 18.0f).text(state().dialogError)
                    .color(kError).fontSize(10.5f).build();
            }
            composeTextButton(ui, "dialog.cancel", width - 204.0f, buttonsY,
                              90.0f, 30.0f, "Cancel", false, true,
                              [] { closeDialog(); });
            composeTextButton(ui, "dialog.import", width - 106.0f, buttonsY,
                              82.0f, 30.0f, "Import", true, !state().busy, [] {
                                  requestFileImport(state().importContent,
                                                    imclash::util::trim(state().importUrl));
                              });
        } else if (state().dialog == DialogKind::ConfirmReplace) {
            ui.text("dialog.title").position(innerX, 18.0f)
                .size(innerW, 24.0f).text("Replace current subscription?")
                .color(kTextPrimary).fontSize(16.0f).build();
            ui.text("dialog.body").position(innerX, 52.0f)
                .size(innerW, 62.0f)
                .text("The new subscription will overwrite config.yaml. "
                      "The previous content will only be kept in memory for rollback.")
                .color(kTextSecondary).fontSize(11.0f).wrap(true).lineHeight(17.0f).build();
            composeTextButton(ui, "dialog.cancel", width - 204.0f, buttonsY,
                              90.0f, 30.0f, "Cancel", false, true,
                              [] { closeDialog(); });
            composeTextButton(ui, "dialog.replace", width - 106.0f, buttonsY,
                              82.0f, 30.0f, "Replace", true, true,
                              [] { executePending(); });
        } else if (state().dialog == DialogKind::CoreUpdate) {
            ui.text("dialog.title").position(innerX, 18.0f)
                .size(innerW, 24.0f).text("mihomo update")
                .color(kTextPrimary).fontSize(16.0f).build();
            if (!state().coreCheckValid) {
                ui.text("dialog.body").position(innerX, 56.0f)
                    .size(innerW, 40.0f).text("Checking the latest release...")
                    .color(kTextSecondary).fontSize(12.0f).build();
            } else {
                const core_update::CheckResult& check = state().coreCheck;
                const std::string current = check.current.empty() ? "not installed"
                                                                  : check.current;
                ui.text("dialog.current").position(innerX, 52.0f)
                    .size(innerW, 18.0f).text("Current: " + current)
                    .color(kTextSecondary).fontSize(11.0f).build();
                ui.text("dialog.latest").position(innerX, 72.0f)
                    .size(innerW, 18.0f).text("Latest: " + check.latest)
                    .color(kTextPrimary).fontSize(11.0f).build();
                composeClippedText(ui, "dialog.asset", innerX, 94.0f, innerW, 18.0f,
                                   check.assetName, kTextSecondary, 10.0f);
                ui.text("dialog.body").position(innerX, 126.0f)
                    .size(innerW, 48.0f)
                    .text(check.updateAvailable
                              ? "A new Windows build is available. The current mihomo "
                                "process will be replaced and restarted."
                              : "The installed mihomo is up to date.")
                    .color(check.updateAvailable ? kWarning : kTextSecondary)
                    .fontSize(11.0f).wrap(true).lineHeight(17.0f).build();
            }
            composeTextButton(ui, "dialog.close", width - 204.0f, buttonsY,
                              90.0f, 30.0f, "Close", false, true,
                              [] { closeDialog(); });
            composeTextButton(ui, "dialog.download", width - 106.0f, buttonsY,
                              82.0f, 30.0f, "Update now", true,
                              state().coreCheckValid &&
                                  state().coreCheck.updateAvailable && !state().busy,
                              [] { downloadCore(); });
        } else if (state().dialog == DialogKind::Error) {
            ui.text("dialog.title").position(innerX, 18.0f)
                .size(innerW, 24.0f).text(state().errorTitle)
                .color(kTextPrimary).fontSize(16.0f).build();
            ui.stack("dialog.errorclip").position(innerX, 52.0f)
                .size(innerW, height - 112.0f).clip().content([&] {
                    ui.text("dialog.errortext").size(innerW, height - 112.0f)
                        .text(state().errorText).color(kTextSecondary)
                        .fontSize(11.0f).wrap(true).lineHeight(17.0f).build();
                }).build();
            composeTextButton(ui, "dialog.ok", width - 106.0f, buttonsY,
                              82.0f, 30.0f, "OK", true, true,
                              [] { closeDialog(); });
        }
    }).build();
}

void initializeAppState() {
    AppState& appState = state();
    const mihomo_process::Paths detected = mihomo_process::paths();
    appState.configDir = detected.directory;
    if (appState.configDir.empty()) {
        showError("Cannot locate the mihomo directory",
                  "The mihomo directory could not be resolved.");
        return;
    }
    std::string error;
    if (!fs::ensureDirectory(appState.configDir, error)) {
        showError("Cannot create the mihomo directory", error);
        return;
    }
    appState.configPath = detected.config.empty()
        ? fs::join(appState.configDir, "config.yaml")
        : detected.config;
    appState.corePath = detected.core.empty()
        ? fs::join(appState.configDir, "mihomo.exe")
        : detected.core;
    appState.corePresent = fs::isRegularFile(appState.corePath);
    appState.autostart = imclash::shell::isAutostartEnabled();
    if (appState.autostart) imclash::shell::setAutostartEnabled(true);
    refreshConfigState();
    if (appState.corePresent) {
        appState.coreVersion = mihomo_process::coreVersion();
    }
    appState.nextConfigCheck = std::chrono::steady_clock::now() + std::chrono::seconds(2);
}

void startupProbe() {
    AppState& appState = state();
    if (!appState.configPresent) {
        appState.startupProbeDone = true;
        return;
    }
    appState.startupProbeDone = false;
    appState.pingInFlight = true;
    const config::Info info = appState.configInfo;
    async::restart(
        "imclash.startup.ping",
        [info]() -> async::Result<std::string> {
            std::string version;
            std::string error;
            if (!mihomo_api::ping(info, version, error)) {
                return async::failure<std::string>(error);
            }
            return async::success(std::move(version));
        },
        [](const async::Result<std::string>& result) {
            AppState& appState = state();
            appState.pingInFlight = false;
            appState.startupProbeDone = true;
            if (result.ok) {
                appState.coreRunning = true;
                appState.coreOwned = false;
                appState.apiReady = true;
                appState.coreStarting = false;
                appState.coreVersion = result.value;
                updateProxyState();
                loadNodes();
                setStatus("Connected to a running mihomo instance.");
            } else if (appState.corePresent) {
                startCore();
            } else {
                setStatus("mihomo.exe was not found. Download it from the Mihomo menu.");
            }
        });
}

void onStart() {
    ensureSingleInstance();
    imclash::shell::setWorkingDirectoryToExecutable();
    initializeAppState();
    imclash::frame::installBorderlessWindow(
        kTitleBarHeight, 192.0f, 264.0f);
    imclash::frame::setTickHandler([] { tick(); });
    if (hasAutostartArgument()) {
        imclash::frame::prepareSilentStartup();
        imclash::frame::scheduleStartupHide();
    } else {
        imclash::frame::centerWindow();
    }
    startupProbe();
    requestUpdate();
}

} // namespace

void compose(eui::Ui& ui, const eui::Screen& screen) {
    ui.stack("root").size(screen.width, screen.height)
        .cursor(eui::CursorShape::Arrow)
        .content([&] {
            ui.rect("root.bg").size(screen.width, screen.height)
                .color(kBackground).build();
            if (state().configPresent) {
                composeSubscriptionView(ui, screen);
            } else {
                composeEmptyState(ui, screen);
            }
            composeStatusBar(ui, screen);
            composeTitleBar(ui, screen);
            composeMenu(ui, screen);
            composeDialog(ui, screen);
        }).build();
}

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("imclash")
        .pageId("imclash")
        .clearColor(kBackground)
        .windowSize(960, 680)
        .minWindowSize(760, 520)
        .decorated(false)
        .showDebugStatsInTitle(false)
        // Empty font paths keep EUI on the system UI font (Segoe UI with the
        // CJK fallback); no font asset is shipped or read.
        .fonts("", "")
        .fps(60.0)
        .tray(true)
        .trayTitle("imclash")
        .trayIcon("imclash.exe")
        .onStart([] { onStart(); });
    return config;
}

} // namespace app
