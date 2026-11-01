#pragma once

#include "config.h"

#include <string>

namespace imclash::mihomo_process {

struct Paths {
    std::string directory;
    std::string config;
    std::string core;
};

Paths paths();
bool coreExists();
bool configExists();

// Runs `mihomo -v` and returns the version token, or an empty string.
std::string coreVersion();

// Runs `mihomo -t -f <path> -d <dir>`.
bool testConfig(const std::string& configPath, std::string& output);

struct StartResult {
    bool ok = false;
    std::string error;
};

StartResult start(const config::Info& info);
void stop();

// Stops an owned core or a running mihomo whose executable is the managed
// core path. Used by the automatic core-update replacement sequence.
bool stopRunning(std::string& error);
bool running();
bool owned();

// Closes process handles after an unexpected exit. Returns true once.
bool poll(std::string& output);
std::string outputTail();

// Used for short-lived helper runs (version, config test).
bool runCapture(const std::string& executable, const std::string& arguments,
                int timeoutMs, std::string& output);

} // namespace imclash::mihomo_process
