#pragma once

#include <string>

namespace imclash::shell {

void setWorkingDirectoryToExecutable();

bool openFolder(const std::string& path);
bool openUrl(const std::string& url);
bool openInEditor(const std::string& path);

bool isAutostartEnabled();
bool setAutostartEnabled(bool enabled);

} // namespace imclash::shell
