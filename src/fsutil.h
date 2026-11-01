#pragma once

#include <cstdint>
#include <string>

namespace imclash::fs {

// Paths are UTF-8; all filesystem calls convert through u8path.
std::string configDirectory();
std::string tempDirectory();
std::string join(const std::string& directory, const std::string& name);

bool readText(const std::string& path, std::string& output, std::string& error);
bool writeTextAtomic(const std::string& path, const std::string& content,
                     std::string& error);
bool ensureDirectory(const std::string& path, std::string& error);
bool isRegularFile(const std::string& path);
std::uint64_t fileSize(const std::string& path);
std::int64_t modifiedSeconds(const std::string& path);
bool removeFile(const std::string& path);
bool removeTree(const std::string& path);

} // namespace imclash::fs
