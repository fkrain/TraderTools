#pragma once

#include "grid/types.hpp"

#include <filesystem>
#include <string>

namespace grid {

// Fills intent.client_order_id if empty. Writes outbox JSON.
bool write_order_json(const std::filesystem::path& orders_dir, OrderIntent& intent, std::string& err);

}  // namespace grid
