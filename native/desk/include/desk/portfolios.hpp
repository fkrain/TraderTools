#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace desk {

// Returns JSON body; sets http_status (200/401/404/502).
std::string portfolios_list_json(const std::string& source, const std::string& rank, int page, int size,
                                 const std::string& q, int& http_status, std::string& err);
std::string portfolio_detail_json(const std::string& source, const std::string& id, int& http_status,
                                  std::string& err);

}  // namespace desk
