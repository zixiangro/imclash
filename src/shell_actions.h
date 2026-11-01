#pragma once

#include <string>

namespace imclash::shell {

std::string executablePath();
std::string executableDirectory();
void setWorkingDirectoryToExecutable();

bool openFolder(const std::string& path);
bool openInEditor(const std::string& path);
bool showInExplorer(const std::string& path);

bool isAutostartEnabled();
bool setAutostartEnabled(bool enabled);

} // namespace imclash::shell
