#include "grid/backtest.hpp"
#include "grid/expr.hpp"
#include "grid/grid_engine.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static int fails = 0;

static void expect(bool cond, const char* name) {
  if (cond) {
    std::cout << "PASS " << name << "\n";
  } else {
    std::cout << "FAIL " << name << "\n";
    ++fails;
  }
}

static grid::Quote qpx(double px) {
  grid::Quote q;
  q.code = "600000.SH";
  q.price = px;
  return q;
}

int main() {
  fs::path dir = fs::temp_directory_path() / "grid_rules_test";
  fs::remove_all(dir);
  fs::create_directories(dir / "outbox");

  {
    grid::EngineConfig cfg;
    cfg.write_orders = false;
    cfg.replay = true;
    cfg.orders_dir = dir;
    grid::GridEngine eng(cfg);
    grid::Strategy s;
    s.code = "600000.SH";
    s.status = "running";
    s.session_enabled = false;
    s.drive = "touch";
    s.pair_mode = "none";
    s.last_price = 10;
    s.buy_qty = 100;
    s.buy_down_pct = 2;
    s.sell_up_pct = 2;
    s.pause_buy = true;
    std::string line;
    eng.replay_quote(s, qpx(9.7), line);
    expect(s.position_shares == 0, "pause_buy blocks buy");
  }

  {
    grid::EngineConfig cfg;
    cfg.write_orders = false;
    cfg.replay = true;
    grid::GridEngine eng(cfg);
    grid::Strategy s;
    s.code = "600000.SH";
    s.status = "running";
    s.session_enabled = false;
    s.drive = "touch";
    s.pair_mode = "none";
    s.last_price = 10;
    s.buy_qty = 100;
    s.buy_down_pct = 2;
    s.position_cap_shares = 100;
    s.position_shares = 100;
    std::string line;
    eng.replay_quote(s, qpx(9.7), line);
    expect(s.position_shares == 100, "share cap blocks buy");
  }

  {
    grid::EngineConfig cfg;
    cfg.write_orders = false;
    cfg.replay = true;
    grid::GridEngine eng(cfg);
    grid::Strategy s;
    s.code = "600000.SH";
    s.status = "running";
    s.session_enabled = false;
    s.drive = "touch";
    s.pair_mode = "strict";
    s.grid_match = true;
    s.last_price = 10;
    s.buy_qty = 100;
    s.buy_down_pct = 2;
    s.unpaired_limit = 1;
    s.match_buy_prices = {9.5};
    std::string line;
    eng.replay_quote(s, qpx(9.6), line);
    expect(s.match_buy_prices.size() == 1, "strict unpaired limit");
  }

  {
    grid::EngineConfig cfg;
    cfg.write_orders = false;
    cfg.replay = true;
    grid::GridEngine eng(cfg);
    grid::Strategy s;
    s.code = "600000.SH";
    s.status = "running";
    s.session_enabled = false;
    s.drive = "touch";
    s.pair_mode = "none";
    s.last_price = 10;
    s.cost_basis = 10;
    s.position_shares = 200;
    s.buy_qty = 100;
    s.sell_qty = 200;
    s.take_profit_mode = "price";
    s.take_profit_value = 11;
    s.rebuild_price = 10;
    std::string line;
    eng.replay_quote(s, qpx(11), line);
    expect(s.phase == "wait_rebuild" && s.position_shares == 0, "take profit flattens");
    eng.replay_quote(s, qpx(10.5), line);
    expect(s.phase == "wait_rebuild", "rebuild waits for price");
    eng.replay_quote(s, qpx(10), line);
    expect(s.phase == "running", "rebuild rearms");
  }

  {
    grid::EngineConfig cfg;
    cfg.write_orders = true;
    cfg.replay = true;
    cfg.orders_dir = dir;
    grid::GridEngine eng(cfg);
    grid::Strategy s;
    s.code = "600000.SH";
    s.status = "running";
    s.session_enabled = false;
    s.drive = "fill";
    s.check_fill = true;
    s.pair_mode = "none";
    s.last_price = 10;
    s.buy_qty = 200;
    s.buy_down_pct = 2;
    std::string line;
    eng.replay_quote(s, qpx(9.7), line);
    expect(!s.pending_order_id.empty() && s.last_price == 10, "fill drive waits");
    nlohmann::json partial = {{"client_order_id", s.pending_order_id},
                              {"status", "partial"},
                              {"filled_qty", 100},
                              {"avg_price", 9.6}};
    fs::create_directories(dir / "partial");
    {
      std::ofstream out(dir / "partial" / "p.json");
      out << partial.dump();
    }
    double base = s.last_price;
    eng.replay_quote(s, qpx(9.7), line);
    expect(s.last_price == base, "partial does not advance base");
    nlohmann::json done = {{"client_order_id", s.pending_order_id},
                           {"status", "filled"},
                           {"filled_qty", 200},
                           {"avg_price", 9.55}};
    fs::create_directories(dir / "done");
    {
      std::ofstream out(dir / "done" / "d.json");
      out << done.dump();
    }
    eng.replay_quote(s, qpx(9.7), line);
    expect(std::abs(s.last_price - 9.55) < 1e-6, "full fill uses avg price");
  }

  {
    fs::path pdir = dir / "prebury";
    fs::create_directories(pdir / "outbox");
    grid::EngineConfig cfg;
    cfg.write_orders = true;
    cfg.replay = true;
    cfg.orders_dir = pdir;
    grid::GridEngine eng(cfg);
    grid::Strategy s;
    s.code = "600000.SH";
    s.status = "running";
    s.session_enabled = false;
    s.drive = "touch";
    s.pair_mode = "none";
    s.preorder_buy = true;
    s.last_price = 10;
    s.buy_qty = 100;
    s.buy_down_pct = 2;
    s.sell_up_pct = 2;
    std::string line;
    eng.replay_quote(s, qpx(9.9), line);
    expect(!s.prebury_buy_id.empty(), "prebury arms inside 1.5%");
    const std::string cid = s.prebury_buy_id;
    eng.replay_quote(s, qpx(10.1), line);
    expect(s.prebury_buy_id.empty(), "prebury cancels outside band");
    expect(fs::exists(pdir / "cancel" / (cid + ".json")), "prebury writes cancel file");
  }

  {
    grid::Strategy s;
    s.code = "600000.SH";
    s.status = "running";
    s.session_enabled = false;
    s.drive = "touch";
    s.pair_mode = "none";
    s.last_price = 10;
    s.position_shares = 1000;
    s.buy_qty = 100;
    s.sell_qty = 100;
    s.buy_down_pct = 2;
    s.sell_up_pct = 2;
    std::vector<grid::MinuteBar> bars = {
        {10, 10, 10, 10},
        {10, 10, 9.8, 9.8},
        {9.8, 10.0, 9.8, 10.0},
    };
    auto report = grid::backtest_grid(s, bars, 100000, 0, 0);
    expect(report["trade_count"].get<int>() >= 2, "backtest trades the same trigger");
    expect(report.contains("hold_equity") && report.contains("grid_equity"), "backtest compares hold");
  }

  {
    std::unordered_map<std::string, double> env{{"CLOSE", 10.5}, {"PRE", 10}, {"ASK1", 10.6}};
    bool out = false;
    std::string err;
    bool ok = grid::eval_expr("(CLOSE/PRE-1)*100 >= 5 AND ASK1 > 0", env, out, err);
    expect(ok && out, "expr all");
    ok = grid::eval_expr("CLOSE < 1 OR PRE = 10", env, out, err);
    expect(ok && out, "expr any via OR");
  }

  std::cout << (fails ? "FAILED " : "OK ") << fails << "\n";
  return fails ? 1 : 0;
}
