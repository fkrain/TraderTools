#pragma once

#include "grid/types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace grid {

bool load_strategies(const std::filesystem::path& path, std::vector<Strategy>& out, std::string& err);
bool save_strategies(const std::filesystem::path& path, const std::vector<Strategy>& items, std::string& err);

}  // namespace grid
