#pragma once

#include "grid/types.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace grid {

bool fetch_quotes(const std::string& source, const std::vector<std::string>& codes,
                  std::unordered_map<std::string, Quote>& out, std::string& err);

bool fetch_sina_quotes(const std::vector<std::string>& codes, std::unordered_map<std::string, Quote>& out,
                       std::string& err);
bool fetch_eastmoney_quotes(const std::vector<std::string>& codes, std::unordered_map<std::string, Quote>& out,
                            std::string& err);

}  // namespace grid
