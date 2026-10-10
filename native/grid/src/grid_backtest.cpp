#include "grid/backtest.hpp"

#include "grid/grid_engine.hpp"

#include <algorithm>
#include <cmath>

namespace grid {
namespace {

double buy_line(const Strategy& s) {
  if (s.last_price <= 0) return 0;
  if (s.by_price) return s.last_price - s.buy_down_pct;
  return s.last_price * (1.0 - s.buy_down_pct / 100.0);
}

double sell_line(const Strategy& s) {
  double base = s.last_price;
  if (s.pair_mode == "strict" && !s.match_buy_prices.empty()) base = s.match_buy_prices.front();
  else if (s.pair_mode == "loose" && s.cost_basis > 0) base = s.cost_basis;
  if (base <= 0) return 0;
  if (s.by_price) return base + s.sell_up_pct;
  return base * (1.0 + s.sell_up_pct / 100.0);
}

}  // namespace

nlohmann::json backtest_grid(Strategy strategy, const std::vector<MinuteBar>& bars, double cash,
                             double commission, double stamp_tax) {
  EngineConfig cfg;
  cfg.write_orders = false;
  cfg.replay = true;
  GridEngine engine(cfg);
  strategy.status = "running";
  strategy.session_enabled = false;
  strategy.drive = "touch";
  strategy.check_fill = false;
  const int start_shares = std::max(0, strategy.position_shares);
  const double start_cash = cash;
  double grid_cash = cash;
  nlohmann::json trades = nlohmann::json::array();

  auto apply_cash = [&](const Strategy& before, const Strategy& after) {
    if (after.last_event.empty() || after.last_event_qty <= 0) return;
    const double px = after.last_event_price;
    const int qty = after.last_event_qty;
    const double notional = px * qty;
    if (after.last_event == "buy") {
      grid_cash -= notional * (1.0 + commission);
    } else if (after.last_event == "sell") {
      grid_cash += notional * (1.0 - commission - stamp_tax);
    }
    trades.push_back({{"side", after.last_event},
                      {"qty", qty},
                      {"price", px},
                      {"cash", grid_cash},
                      {"position", after.position_shares}});
    (void)before;
  };

  for (const auto& bar : bars) {
    if (bar.high <= 0 || bar.low <= 0) continue;
    double bline = buy_line(strategy);
    double sline = sell_line(strategy);
    const bool hit_buy = bline > 0 && bar.low <= bline && !strategy.pause_buy;
    const bool hit_sell = sline > 0 && bar.high >= sline && !strategy.pause_sell;
    std::vector<double> path;
    if (hit_buy && hit_sell) {
      if (bar.open <= bline) {
        path.push_back(bline);
        path.push_back(sline);
      } else if (bar.open >= sline) {
        path.push_back(sline);
        path.push_back(bline);
      } else if (bar.close >= bar.open) {
        path.push_back(bline);
        path.push_back(sline);
      } else {
        path.push_back(sline);
        path.push_back(bline);
      }
    } else if (hit_buy) {
      path.push_back(bline);
    } else if (hit_sell) {
      path.push_back(sline);
    } else {
      path.push_back(bar.close > 0 ? bar.close : bar.open);
    }
    for (double px : path) {
      if (px <= 0) continue;
      Strategy before = strategy;
      Quote q;
      q.code = strategy.code;
      q.price = px;
      q.open = bar.open;
      q.high = bar.high;
      q.low = bar.low;
      std::string line;
      engine.replay_quote(strategy, q, line);
      apply_cash(before, strategy);
      if (strategy.status != "running" && strategy.phase != "wait_rebuild") break;
    }
  }

  const double mark = bars.empty() ? strategy.last_price : bars.back().close;
  const double grid_equity = grid_cash + strategy.position_shares * mark;
  const double hold_equity = start_cash + start_shares * mark;
  return nlohmann::json{
      {"trades", trades},
      {"trade_count", trades.size()},
      {"end_shares", strategy.position_shares},
      {"cash", grid_cash},
      {"mark", mark},
      {"grid_equity", grid_equity},
      {"hold_equity", hold_equity},
      {"range_pnl", strategy.range_pnl},
      {"last_price", strategy.last_price},
  };
}

}  // namespace grid
