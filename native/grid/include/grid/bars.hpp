#pragma once

#include "grid/types.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace grid {

struct IndicatorSnap {
  double ma5 = 0;
  double ma10 = 0;
  double ma20 = 0;
  double ma60 = 0;
  double macd_cross_buy = 0;
  double macd_cross_sell = 0;
  double kdj_cross_buy = 0;
  double kdj_cross_sell = 0;
  double boll_up = 0;
  double boll_mid = 0;
  double boll_dn = 0;
};

void note_quote_bar(const std::filesystem::path& data_dir, const Quote& q, long long epoch_sec);
IndicatorSnap indicators_for(const std::filesystem::path& data_dir, const std::string& code, int minutes);
void fill_indicator_env(const IndicatorSnap& ind, std::unordered_map<std::string, double>& env, int minutes);

}  // namespace grid
