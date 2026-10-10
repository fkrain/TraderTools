#pragma once

#include "grid/types.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace grid {

class GridEngine {
 public:
  explicit GridEngine(EngineConfig cfg);

  bool load(std::string& err);
  bool save(std::string& err);
  void tick(std::string& log_line);
  void run_loop(std::atomic<bool>& running);

  const std::vector<Strategy>& strategies() const { return strategies_; }
  // Backtest / tests: one quote, no disk reload. Respects cfg.replay and cfg.write_orders.
  bool replay_quote(Strategy& s, const Quote& q, std::string& log_line);

 private:
  int calc_buy_qty(const Strategy& s, double price) const;
  int calc_sell_qty(const Strategy& s, double price) const;
  bool eval_and_trade(Strategy& s, const Quote& q, std::string& log_line);
  void reload_from_disk();
  // returns: 0=still waiting, 1=resolved ok (may have applied fill), -1=rejected/cleared without fill
  int resolve_pending(Strategy& s, std::string& note);
  void clear_pending(Strategy& s);
  void apply_fill(Strategy& s, double mark_price);
  bool emit_order(Strategy& s, const std::string& side, int qty, double price, const std::string& name,
                  const std::string& reason, std::string& err);

  EngineConfig cfg_;
  std::vector<Strategy> strategies_;
  std::string last_emitted_id_;
};

}  // namespace grid
