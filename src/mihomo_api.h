#pragma once

#include "config.h"

#include <string>
#include <vector>

namespace imclash::mihomo_api {

struct Node {
    std::string name;
    std::string type;
    int delay = -1;
};

struct Group {
    std::string name;
    std::string type;
    std::string now;
    std::vector<std::string> all;
};

bool ping(const config::Info& info, std::string& version, std::string& error);
bool fetchState(const config::Info& info, std::vector<Node>& nodes,
                std::vector<Group>& groups, std::string& error);
bool fetchProxies(const config::Info& info, std::vector<Node>& nodes,
                  std::string& error);
bool selectNode(const config::Info& info, const std::string& group,
                const std::string& node, std::string& error);
bool testDelay(const config::Info& info, const std::string& name,
               int timeoutMs, const std::string& testUrl, int& delay,
               std::string& error);
bool reload(const config::Info& info, const std::string& configPath,
            std::string& error);

} // namespace imclash::mihomo_api
