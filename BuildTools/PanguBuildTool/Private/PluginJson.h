#pragma once

#include "PluginDiscovery.h"

#include <string>
#include <vector>

namespace pg::build::plugin
{
[[nodiscard]] std::string PluginsToJson(const std::vector<PluginEntry>& plugins);
} // namespace pg::build::plugin
