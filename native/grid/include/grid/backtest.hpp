#pragma once

#include "grid/types.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace grid {

struct MinuteBar {
  double open = 0;
  double high = 0;
  double low = 0;
  double close = 0;
};

// Replays bars through GridEngine::replay_quote (same trigger as live).
// Fills are assumed at the grid line when that line is touched inside the bar.
nlohmann::json backtest_grid(Strategy strategy, const std::vector<MinuteBar>& bars, double cash,
                             double commission, double stamp_tax);

}  // namespace grid
