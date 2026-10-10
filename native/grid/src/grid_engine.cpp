#include "grid/grid_engine.hpp"

#include "grid/bars.hpp"
#include "grid/json_io.hpp"
#include "grid/order_file.hpp"
#include "grid/quote_factory.hpp"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace grid {
namespace {

int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool clock_at_or_after_1500() {
  using clock = std::chrono::system_clock;
  auto t = clock::to_time_t(clock::now());
  std::tm tm{};
  localtime_s(&tm, &t);
  return tm.tm_hour > 15 || (tm.tm_hour == 15 && tm.tm_min >= 0);
}

void write_cancel(const std::filesystem::path& orders_dir, const std::string& cid) {
  if (cid.empty() || orders_dir.empty()) return;
  std::error_code ec;
  std::filesystem::create_directories(orders_dir / "cancel", ec);
  std::ofstream out(orders_dir / "cancel" / (cid + ".json"));
  if (!out) return;
  out << nlohmann::json{{"client_order_id", cid}, {"status", "cancelled"}, {"reason", "prebury"}}.dump();
}

int floor_lot(int qty, const std::string& code) {
  int lot = 100;
  auto pos = code.find('.');
  std::string num = pos == std::string::npos ? code : code.substr(0, pos);
  if (num.size() >= 2 && (num.substr(0, 2) == "11" || num.substr(0, 2) == "12")) lot = 10;
  if (qty < lot) return 0;
  return (qty / lot) * lot;
}

std::string upper(std::string s) {
  for (auto& c : s) c = (char)toupper((unsigned char)c);
  return s;
}

int parse_hms(const std::string& t) {
  // "HH:MM:SS" or "HH:MM" -> seconds since midnight
  int h = 0, m = 0, s = 0;
  if (t.size() >= 5) {
    h = std::atoi(t.substr(0, 2).c_str());
    m = std::atoi(t.substr(3, 2).c_str());
  }
  if (t.size() >= 8) s = std::atoi(t.substr(6, 2).c_str());
  return h * 3600 + m * 60 + s;
}

bool in_session(const Strategy& s) {
  if (!s.session_enabled) return true;
  using clock = std::chrono::system_clock;
  auto t = clock::to_time_t(clock::now());
  std::tm tm{};
  localtime_s(&tm, &t);
  int now = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
  int a = parse_hms(s.session_start.empty() ? "09:25:00" : s.session_start);
  int b = parse_hms(s.session_end.empty() ? "15:00:00" : s.session_end);
  if (a <= b) return now >= a && now <= b;
  return now >= a || now <= b;
}

double level_price(const double levels[5], int idx, double fallback) {
  if (idx < 0 || idx > 4) return fallback;
  if (levels[idx] > 0) return levels[idx];
  // degrade to nearest available better-of-book
  for (int i = idx; i >= 0; --i)
    if (levels[i] > 0) return levels[i];
  for (int i = idx + 1; i < 5; ++i)
    if (levels[i] > 0) return levels[i];
  return fallback;
}

double pick_quote_price(const Quote& q, const std::string& type) {
  if (type == "market" || type == "last" || type == "newest") return q.price;
  if (type.rfind("bid", 0) == 0 && type.size() == 4 && std::isdigit((unsigned char)type[3])) {
    int n = type[3] - '1';
    double fb = q.bid > 0 ? q.bid : q.price;
    return level_price(q.bids, n, fb);
  }
  if (type.rfind("ask", 0) == 0 && type.size() == 4 && std::isdigit((unsigned char)type[3])) {
    int n = type[3] - '1';
    double fb = q.ask > 0 ? q.ask : q.price;
    return level_price(q.asks, n, fb);
  }
  if (type == "buy" || type == "bid") return q.bid > 0 ? q.bid : q.price;
  if (type == "sell" || type == "ask") return q.ask > 0 ? q.ask : q.price;
  return q.price;
}

double apply_offset(double px, int cents, bool buy) {
  if (px <= 0) return px;
  double d = cents / 100.0;
  // buy: pay up to improve fill; sell: shade down
  return buy ? (px + d) : (px - d);
}

bool is_market_price_type(const std::string& t) { return t == "market"; }

double order_price_for(const Strategy& s, const Quote& q, bool buy) {
  const std::string& t = buy ? s.buy_price_type : s.sell_price_type;
  if (is_market_price_type(t)) return q.price;
  double px = pick_quote_price(q, t);
  return apply_offset(px, s.price_offset_cents, buy);
}

std::string normalize_bound_action(std::string a, const std::string& fallback) {
  for (auto& c : a) c = (char)tolower((unsigned char)c);
  if (a == "pause" || a == "clear_pause" || a == "clear_stop") return a;
  if (a == "clear" || a == "clearstop" || a == "stop") return "clear_stop";
  if (a == "clearpause" || a == "clear_and_pause") return "clear_pause";
  return fallback;
}

}  // namespace

GridEngine::GridEngine(EngineConfig cfg) : cfg_(std::move(cfg)) {}

bool GridEngine::load(std::string& err) {
  return load_strategies(cfg_.grid_json, strategies_, err);
}

bool GridEngine::save(std::string& err) {
  return save_strategies(cfg_.grid_json, strategies_, err);
}

int GridEngine::calc_buy_qty(const Strategy& s, double price) const {
  if (price <= 0) return 0;
  double raw = s.buy_qty;
  if (s.force_buy_qty > 0) {
    raw = s.force_buy_qty;
  } else if (s.buy_qty_multiple > 0 && s.last_qty > 0) {
    raw = s.last_qty * s.buy_qty_multiple;
  } else if (s.buy_unit == "amount") {
    raw = s.buy_qty / price;
  }
  if (s.double_add_price > 0 && price <= s.double_add_price && s.buy_qty_multiple <= 0 && s.force_buy_qty <= 0) {
    raw *= 2.0;
  }
  if (s.burst_buy_lots > 1) raw *= s.burst_buy_lots;
  if (s.cross_buy_multiply && s.last_price > 0 && s.buy_down_pct > 0) {
    double drop = (s.last_price - price) / s.last_price * 100.0;
    double grids = drop / s.buy_down_pct;
    if (grids > 1.0) raw *= std::floor(grids);
  }
  int qty = floor_lot((int)std::llround(raw), s.code);
  if (qty <= 0) return 0;
  if (s.max_position_amount > 0) {
    double cur = std::max(0, s.position_shares) * price;
    double room = s.max_position_amount - cur;
    if (room <= 0) return 0;
    int max_q = floor_lot((int)std::floor(room / price), s.code);
    if (max_q < qty) qty = max_q;
  }
  if (s.position_cap_shares > 0) {
    int room = s.position_cap_shares - std::max(0, s.position_shares);
    if (room <= 0) return 0;
    room = floor_lot(room, s.code);
    if (room < qty) qty = room;
  }
  return qty;
}

int GridEngine::calc_sell_qty(const Strategy& s, double price) const {
  if (price <= 0) return 0;
  double raw = s.sell_qty;
  if (s.force_sell_qty > 0) {
    raw = s.force_sell_qty;
  } else if (s.pair_mode == "strict" && s.buy_qty > 0) {
    raw = s.buy_qty;
  } else if (raw <= 0) {
    raw = s.last_qty;
  } else if (s.sell_unit == "amount" && s.sell_qty > 0) {
    raw = s.sell_qty / price;
  }
  if (s.burst_sell_lots > 1 && s.force_sell_qty <= 0) raw *= s.burst_sell_lots;
  if (s.sell_all_after_multi_buy && s.buy_streak > 1 && s.last_qty > 0) {
    raw = s.last_qty;
  }
  if (s.cross_sell_multiply && s.last_price > 0 && s.sell_up_pct > 0) {
    double rise = (price - s.last_price) / s.last_price * 100.0;
    double grids = rise / s.sell_up_pct;
    if (grids > 1.0) raw *= std::floor(grids);
  }
  int qty = floor_lot((int)std::llround(raw), s.code);
  if (qty <= 0) return 0;
  if (s.reserve_base_shares > 0 && s.position_shares > 0) {
    int sellable = s.position_shares - s.reserve_base_shares;
    if (sellable <= 0) return 0;
    sellable = floor_lot(sellable, s.code);
    if (sellable < qty) qty = sellable;
  }
  if (s.available_shares >= 0 && qty > s.available_shares) {
    qty = floor_lot(s.available_shares, s.code);
  }
  return qty;
}

void GridEngine::reload_from_disk() {
  std::vector<Strategy> fresh;
  std::string err;
  if (!load_strategies(cfg_.grid_json, fresh, err)) return;
  std::unordered_map<std::string, Strategy> runtime;
  for (const auto& s : strategies_) runtime[s.id] = s;
  for (auto& s : fresh) {
    auto it = runtime.find(s.id);
    if (it == runtime.end()) continue;
    s.pivot_touch_ms = it->second.pivot_touch_ms;
    s.sell_armed = it->second.sell_armed;
    s.buy_armed = it->second.buy_armed;
    if (s.pending_order_id.empty()) {
      s.pending_order_id = it->second.pending_order_id;
      s.pending_side = it->second.pending_side;
      s.pending_qty = it->second.pending_qty;
      s.pending_price = it->second.pending_price;
      s.pending_ref_price = it->second.pending_ref_price;
      s.pending_grid_match_pop = it->second.pending_grid_match_pop;
      s.pending_grid_match_push = it->second.pending_grid_match_push;
    }
    // keep engine-owned stacks if disk empty / stale
    if (s.match_buy_prices.empty() && !it->second.match_buy_prices.empty()) {
      s.match_buy_prices = it->second.match_buy_prices;
    }
    if (s.position_shares == 0 && it->second.position_shares > 0) {
      s.position_shares = it->second.position_shares;
    }
  }
  strategies_ = std::move(fresh);
}

void GridEngine::clear_pending(Strategy& s) {
  s.pending_order_id.clear();
  s.pending_side.clear();
  s.pending_qty = 0;
  s.pending_price = 0;
  s.pending_ref_price = 0;
  s.pending_grid_match_pop = false;
  s.pending_grid_match_push = false;
}

void GridEngine::apply_fill(Strategy& s, double mark_price) {
  const std::string side = s.pending_side;
  const int qty = s.pending_qty;
  const double opx = s.pending_price > 0 ? s.pending_price : mark_price;
  const double ref = s.pending_ref_price > 0 ? s.pending_ref_price : s.last_price;
  const bool pop_match = s.pending_grid_match_pop;
  const bool push_match = s.pending_grid_match_push;
  clear_pending(s);
  s.last_event.clear();
  s.last_event_qty = 0;
  s.last_event_price = 0;
  if (qty <= 0 || side.empty()) return;

  if (side == "sell") {
    double match_base = ref;
    if (pop_match && !s.match_buy_prices.empty()) {
      match_base = s.match_buy_prices.front();
      s.match_buy_prices.erase(s.match_buy_prices.begin());
    }
    s.range_pnl += (opx - match_base) * qty;
    if (s.use_order_price) s.last_price = opx;
    s.last_qty = qty;
    s.buy_streak = 0;
    s.sell_streak += 1;
    s.position_shares = std::max(0, s.position_shares - qty);
    if (s.available_shares >= 0) s.available_shares = std::max(0, s.available_shares - qty);
    s.high_pivot = mark_price > 0 ? mark_price : opx;
    s.low_pivot = s.high_pivot;
    s.sell_armed = false;
    s.pivot_touch_ms = 0;
    if (s.stop_when_flat && s.position_shares <= 0 && mark_price > ref) s.status = "stopped";
    s.last_event = "sell";
  } else if (side == "buy") {
    int old_pos = std::max(0, s.position_shares);
    if (old_pos > 0 && s.cost_basis > 0)
      s.cost_basis = (s.cost_basis * old_pos + opx * qty) / (old_pos + qty);
    else
      s.cost_basis = opx;
    double save = (ref - opx) * qty;
    if (save > 0) s.range_save += save;
    if (s.use_order_price) s.last_price = opx;
    s.last_qty = qty;
    s.buy_streak += 1;
    s.sell_streak = 0;
    s.position_shares += qty;
    if (push_match) s.match_buy_prices.push_back(opx);
    s.high_pivot = mark_price > 0 ? mark_price : opx;
    s.low_pivot = s.high_pivot;
    s.buy_armed = false;
    s.pivot_touch_ms = 0;
    s.last_event = "buy";
  }
  s.last_event_qty = qty;
  s.last_event_price = opx;
  s.burst_buy_lots = 1;
  s.burst_sell_lots = 1;
}

int GridEngine::resolve_pending(Strategy& s, std::string& note) {
  if (s.pending_order_id.empty()) return 1;
  namespace fs = std::filesystem;
  const std::string& cid = s.pending_order_id;
  // 2 = partial (do not advance). 1 = full fill. 0 = still working. -1 = dead.
  for (const char* folder : {"done", "partial", "ack", "reject", "cancel"}) {
    std::error_code ec;
    auto dir = cfg_.orders_dir / folder;
    if (!fs::exists(dir, ec)) continue;
    for (auto& ent : fs::directory_iterator(dir, ec)) {
      if (ent.path().extension() != ".json") continue;
      try {
        std::ifstream in(ent.path());
        nlohmann::json j;
        in >> j;
        std::string id = j.value("client_order_id", "");
        if (id.empty()) id = ent.path().stem().string();
        if (id.find(cid) == std::string::npos && id != cid) continue;
        const std::string folder_s(folder);
        std::string status = j.value("status", "");
        for (auto& c : status) c = (char)tolower((unsigned char)c);
        int filled = 0;
        if (j.contains("filled_qty") && j["filled_qty"].is_number()) filled = j["filled_qty"].get<int>();
        double avg = 0;
        if (j.contains("avg_price") && j["avg_price"].is_number()) avg = j["avg_price"].get<double>();
        if (avg <= 0 && j.contains("price") && j["price"].is_number()) avg = j["price"].get<double>();
        const bool cancelled = folder_s == "reject" || folder_s == "cancel" || status == "cancelled" ||
                               status == "rejected";
        if (cancelled) {
          note = "order rejected " + cid;
          clear_pending(s);
          return -1;
        }
        const bool partial = folder_s == "partial" || status == "partial" ||
                             (filled > 0 && s.pending_qty > 0 && filled < s.pending_qty && status != "filled");
        if (partial) {
          note = "partial " + cid + " filled=" + std::to_string(filled);
          return 2;
        }
        if (folder_s == "ack" && status != "filled") {
          note = "acked " + cid;
          return 0;
        }
        if (avg > 0) s.pending_price = avg;
        if (filled > 0) s.pending_qty = filled;
        note = std::string("filled via ") + folder + " " + cid;
        return 1;
      } catch (...) {
      }
    }
  }
  for (const char* folder : {"outbox", "sent"}) {
    std::error_code ec;
    auto dir = cfg_.orders_dir / folder;
    if (!fs::exists(dir, ec)) continue;
    for (auto& ent : fs::directory_iterator(dir, ec)) {
      auto name = ent.path().filename().string();
      if (name.find(cid) != std::string::npos) {
        note = std::string("waiting ") + folder + " " + cid;
        return 0;
      }
    }
  }
  note = "pending cleared (no file) " + cid;
  clear_pending(s);
  return -1;
}

bool GridEngine::emit_order(Strategy& s, const std::string& side, int qty, double price, const std::string& name,
                            const std::string& reason, std::string& err) {
  if (!cfg_.write_orders) return true;
  OrderIntent intent;
  intent.code = upper(s.code);
  intent.side = side;
  intent.qty = qty;
  intent.price = price;
  intent.name = name;
  intent.reason = reason;
  intent.margin_buy = (side == "buy" && s.margin_buy);
  const std::string& pxt = side == "buy" ? s.buy_price_type : s.sell_price_type;
  intent.price_type = is_market_price_type(pxt) ? "market" : "limit";
  if (!write_order_json(cfg_.orders_dir, intent, err)) return false;
  last_emitted_id_ = intent.client_order_id;
  if (s.check_fill || s.drive == "fill") s.pending_order_id = intent.client_order_id;
  return true;
}

bool GridEngine::replay_quote(Strategy& s, const Quote& q, std::string& log_line) {
  return eval_and_trade(s, q, log_line);
}

bool GridEngine::eval_and_trade(Strategy& s, const Quote& q, std::string& log_line) {
  if (s.status == "paused" && s.paused_by_bound && q.price > 0) {
    const bool below_ceil = s.price_ceil <= 0 || q.price < s.price_ceil;
    const bool above_floor = s.price_floor <= 0 || q.price > s.price_floor;
    if (below_ceil && above_floor) {
      s.status = "running";
      s.paused_by_bound = false;
      log_line = s.code + " back inside range";
      return true;
    }
    return false;
  }
  if (s.status != "running") return false;
  if (q.price <= 0) return false;

  if (!cfg_.replay && !in_session(s)) {
    log_line = s.code + " outside session";
    return false;
  }

  auto grid_px = [&](bool buy) {
    if (s.last_price <= 0) return 0.0;
    if (s.by_price) return buy ? s.last_price - s.buy_down_pct : s.last_price + s.sell_up_pct;
    return buy ? s.last_price * (1.0 - s.buy_down_pct / 100.0) : s.last_price * (1.0 + s.sell_up_pct / 100.0);
  };
  auto drop_prebury = [&](std::string& id) {
    if (id.empty()) return;
    if (cfg_.write_orders) write_cancel(cfg_.orders_dir, id);
    if (s.pending_order_id == id) clear_pending(s);
    id.clear();
  };
  if (s.pair_mode == "none" && (!s.prebury_buy_id.empty() || !s.prebury_sell_id.empty())) {
    const double band = s.prebury_band_pct > 0 ? s.prebury_band_pct : 1.5;
    const bool session_end = !cfg_.replay && clock_at_or_after_1500();
    auto left_band = [&](bool buy, const std::string& id) {
      if (id.empty() || session_end) return session_end && !id.empty();
      const double gp = grid_px(buy);
      if (gp <= 0 || q.price <= 0) return false;
      const double dist = std::abs(q.price - gp) / gp * 100.0;
      const bool still_approaching = buy ? (q.price > gp) : (q.price < gp);
      return still_approaching && dist > band;
    };
    if (session_end || left_band(true, s.prebury_buy_id)) drop_prebury(s.prebury_buy_id);
    if (session_end || left_band(false, s.prebury_sell_id)) drop_prebury(s.prebury_sell_id);
  }

  if (s.pending_order_id.empty() && !s.prebury_buy_id.empty()) {
    s.pending_order_id = s.prebury_buy_id;
    s.pending_side = "buy";
    s.pending_qty = std::max(s.pending_qty, calc_buy_qty(s, q.price));
    s.pending_ref_price = s.last_price;
  } else if (s.pending_order_id.empty() && !s.prebury_sell_id.empty()) {
    s.pending_order_id = s.prebury_sell_id;
    s.pending_side = "sell";
    s.pending_qty = std::max(s.pending_qty, calc_sell_qty(s, q.price));
    s.pending_ref_price = s.last_price;
  }

  if (!s.pending_order_id.empty()) {
    std::string note;
    int st = resolve_pending(s, note);
    if (st == 0 || st == 2) {
      log_line = s.code + " " + note;
      return false;
    }
    if (st > 0 && !s.pending_side.empty()) {
      const std::string cid = s.pending_order_id;
      const std::string filled_side = s.pending_side;
      apply_fill(s, q.price);
      if (s.prebury_buy_id == cid) s.prebury_buy_id.clear();
      if (s.prebury_sell_id == cid) s.prebury_sell_id.clear();
      if (filled_side == "buy") drop_prebury(s.prebury_sell_id);
      if (filled_side == "sell") drop_prebury(s.prebury_buy_id);
      log_line = s.code + " " + note + " state applied";
      return true;
    }
    if (st > 0) {
      // orphan pending id without snapshot — drop gate
      clear_pending(s);
      log_line = s.code + " " + note + " (no deferred state)";
      return true;
    }
    if (st < 0) {
      log_line = s.code + " " + note;
      return true;  // persist cleared pending
    }
  }

  if (s.last_price <= 0) {
    s.last_price = q.price;
    if (s.cost_basis <= 0) s.cost_basis = q.price;
    s.low_pivot = q.price;
    s.high_pivot = q.price;
    log_line = s.code + " init last_price=" + std::to_string(q.price);
    return true;
  }

  if (s.phase == "wait_rebuild") {
    if (s.rebuild_price > 0 && q.price <= s.rebuild_price) {
      s.phase = "running";
      s.last_price = q.price;
      log_line = s.code + " rebuild armed @" + std::to_string(q.price);
      return true;
    }
    log_line = s.code + " wait rebuild";
    return false;
  }

  auto apply_bound = [&](const char* which, const std::string& action_raw,
                         const std::string& fallback) -> bool {
    const std::string action = normalize_bound_action(action_raw, fallback);
    const bool do_clear = (action == "clear_pause" || action == "clear_stop");
    int sold = 0;
    if (do_clear) {
      int qty = 0;
      if (s.position_shares > s.reserve_base_shares) {
        qty = s.position_shares - s.reserve_base_shares;
      } else if (s.position_shares <= 0) {
        qty = s.last_qty > 0 ? s.last_qty : calc_sell_qty(s, q.price);
      }
      qty = floor_lot(qty, s.code);
      if (qty > 0) {
        std::string err;
        double opx = order_price_for(s, q, false);
        if (emit_order(s, "sell", qty, opx > 0 ? opx : q.price, s.name.empty() ? q.name : s.name,
                       std::string(which) + "_clear", err)) {
          sold = qty;
          if (s.position_shares > 0) s.position_shares = std::max(0, s.position_shares - qty);
        }
      }
    }
    if (action == "clear_stop") {
      s.status = "stopped";
      s.paused_by_bound = false;
    } else {
      s.status = "paused";
      s.paused_by_bound = (action == "pause");
    }
    s.last_price = q.price;
    log_line = s.code + " hit " + which + " (" + action + ")";
    if (sold > 0) log_line += " sold=" + std::to_string(sold);
    return true;
  };

  if (s.price_ceil > 0 && q.price >= s.price_ceil) {
    return apply_bound("ceil", s.ceil_action, "pause");
  }

  if (s.price_floor > 0 && q.price <= s.price_floor) {
    return apply_bound("floor", s.floor_action, "clear_stop");
  }

  auto flatten = [&](const std::string& reason, const std::string& next_phase) -> bool {
    int qty = s.position_shares - std::max(0, s.reserve_base_shares);
    if (s.available_shares >= 0) qty = std::min(qty, s.available_shares);
    qty = floor_lot(qty, s.code);
    if (qty <= 0) {
      if (next_phase == "wait_rebuild") s.phase = next_phase;
      if (reason == "grid_stop") s.status = "paused";
      log_line = s.code + " " + reason + " qty=0";
      return true;
    }
    double opx = order_price_for(s, q, false);
    if (opx <= 0) opx = q.price;
    std::string err;
    if (!emit_order(s, "sell", qty, opx, s.name.empty() ? q.name : s.name, reason, err)) {
      log_line = s.code + " " + reason + " failed: " + err;
      return false;
    }
    const bool wait_fill = cfg_.write_orders && (s.drive == "fill" || s.check_fill);
    s.pending_side = "sell";
    s.pending_qty = qty;
    s.pending_price = opx;
    s.pending_ref_price = s.last_price;
    s.pending_grid_match_pop = false;
    s.pending_grid_match_push = false;
    if (wait_fill) {
      log_line = s.code + " " + reason + " await fill";
      return true;
    }
    apply_fill(s, q.price);
    if (next_phase == "wait_rebuild") s.phase = "wait_rebuild";
    if (reason == "grid_stop") s.status = "paused";
    log_line = s.code + " " + reason + " " + std::to_string(qty);
    return true;
  };

  if (s.stop_price > 0 && q.price <= s.stop_price) return flatten("grid_stop", "");

  if (s.take_profit_mode == "price" && s.take_profit_value > 0 && q.price >= s.take_profit_value) {
    return flatten("grid_take", "wait_rebuild");
  }
  if (s.take_profit_mode == "pnl_pct" && s.take_profit_value > 0 && s.cost_basis > 0 && s.position_shares > 0) {
    double pnl = (q.price - s.cost_basis) / s.cost_basis * 100.0;
    if (pnl >= s.take_profit_value) return flatten("grid_take", "wait_rebuild");
  }

  {
    using clock = std::chrono::system_clock;
    auto t = clock::to_time_t(clock::now());
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    std::string day(buf);
    s.burst_buy_lots = 1;
    s.burst_sell_lots = 1;
    if (s.gap_open_lots && s.day_key != day) {
      const bool prebury = s.pair_mode == "none" && (s.preorder_buy || s.preorder_sell);
      double gap = (q.price - s.last_price) / s.last_price * 100.0;
      if (!prebury && s.buy_down_pct > 0 && gap <= -s.buy_down_pct) {
        int lots = (int)std::floor((-gap) / s.buy_down_pct);
        if (lots > 1) s.burst_buy_lots = lots;
      }
      if (!prebury && s.sell_up_pct > 0 && gap >= s.sell_up_pct) {
        int lots = (int)std::floor(gap / s.sell_up_pct);
        if (lots > 1) s.burst_sell_lots = lots;
      }
      s.day_key = day;
    }
  }

  if (s.low_pivot <= 0 || q.price < s.low_pivot) s.low_pivot = q.price;
  if (s.high_pivot <= 0 || q.price > s.high_pivot) s.high_pivot = q.price;

  const double base = s.last_price;
  bool want_sell = false;
  bool want_buy = false;

  if (s.by_price) {
    want_sell = q.price >= base + s.sell_up_pct;
    want_buy = q.price <= base - s.buy_down_pct;
  } else {
    want_sell = q.price >= base * (1.0 + s.sell_up_pct / 100.0);
    want_buy = q.price <= base * (1.0 - s.buy_down_pct / 100.0);
  }

  const bool stack_pair = s.pair_mode == "strict" || (s.pair_mode.empty() && s.grid_match);
  if (stack_pair && !s.match_buy_prices.empty()) {
    double buy_px = s.match_buy_prices.front();
    if (s.by_price) want_sell = q.price >= buy_px + s.sell_up_pct;
    else want_sell = q.price >= buy_px * (1.0 + s.sell_up_pct / 100.0);
  } else if (s.pair_mode == "loose" && s.cost_basis > 0) {
    if (s.by_price) want_sell = q.price >= s.cost_basis + s.sell_up_pct;
    else want_sell = q.price >= s.cost_basis * (1.0 + s.sell_up_pct / 100.0);
  }

  if (s.pair_mode == "strict" && s.unpaired_limit > 0 &&
      (int)s.match_buy_prices.size() >= s.unpaired_limit) {
    want_buy = false;
  }
  if (s.pause_buy) want_buy = false;
  if (s.pause_sell) want_sell = false;

  if (s.pullback_pct > 0) {
    if (want_sell) {
      s.sell_armed = true;
      if (s.pivot_touch_ms == 0) s.pivot_touch_ms = now_ms();
      double from_high = (s.high_pivot - q.price) / s.high_pivot * 100.0;
      want_sell = from_high >= s.pullback_pct;
      if (s.check_sell_with_up && want_sell) {
        want_sell = q.price >= base * (1.0 + s.sell_up_pct / 100.0);
      }
    } else {
      s.sell_armed = false;
    }
  }
  if (s.rebound_pct > 0) {
    if (want_buy) {
      s.buy_armed = true;
      if (s.pivot_touch_ms == 0) s.pivot_touch_ms = now_ms();
      double from_low = (q.price - s.low_pivot) / s.low_pivot * 100.0;
      want_buy = from_low >= s.rebound_pct;
      if (s.check_buy_with_down && want_buy) {
        want_buy = q.price <= base * (1.0 - s.buy_down_pct / 100.0);
      }
    } else {
      s.buy_armed = false;
    }
  }

  if ((s.rebound_pct > 0 || s.pullback_pct > 0) && s.delay_confirm_sec > 0) {
    if (s.pivot_touch_ms > 0 && now_ms() - s.pivot_touch_ms < s.delay_confirm_sec * 1000LL) {
      want_buy = false;
      want_sell = false;
    }
  }

  if (s.max_buy_streak > 0 && s.buy_streak >= s.max_buy_streak) want_buy = false;
  if (s.max_sell_streak > 0 && s.sell_streak >= s.max_sell_streak) want_sell = false;
  if (s.position_cap_shares > 0 && s.position_shares >= s.position_cap_shares) want_buy = false;

  auto line_px = [&](bool buy) {
    if (s.by_price) return buy ? base - s.buy_down_pct : base + s.sell_up_pct;
    return buy ? base * (1.0 - s.buy_down_pct / 100.0) : base * (1.0 + s.sell_up_pct / 100.0);
  };
  const bool wait_fill = cfg_.write_orders && (s.drive == "fill" || s.check_fill);
  const bool prebury_ok = s.pair_mode == "none" && (s.preorder_buy || s.preorder_sell);
  if (prebury_ok && !want_buy && !want_sell && s.pending_order_id.empty()) {
    const double band = s.prebury_band_pct > 0 ? s.prebury_band_pct : 1.5;
    auto arm = [&](bool buy) {
      std::string& id = buy ? s.prebury_buy_id : s.prebury_sell_id;
      if (!(buy ? s.preorder_buy : s.preorder_sell)) return;
      double gp = line_px(buy);
      if (gp <= 0) return;
      double dist = std::abs(q.price - gp) / gp * 100.0;
      bool approaching = buy ? (q.price > gp && dist <= band) : (q.price < gp && dist <= band);
      if (approaching && id.empty()) {
        int qty = buy ? calc_buy_qty(s, gp) : calc_sell_qty(s, gp);
        if (qty <= 0) return;
        std::string err;
        if (!emit_order(s, buy ? "buy" : "sell", qty, gp, s.name.empty() ? q.name : s.name,
                        buy ? "grid_prebury_buy" : "grid_prebury_sell", err))
          return;
        id = last_emitted_id_;
        if (s.pending_order_id == id && s.drive != "fill" && !s.check_fill) s.pending_order_id.clear();
        log_line = s.code + std::string(buy ? " prebury buy @" : " prebury sell @") + std::to_string(gp);
      }
    };
    arm(true);
    if (log_line.empty()) arm(false);
    if (!log_line.empty()) return true;
  }

  if (want_sell) {
    int qty = calc_sell_qty(s, q.price);
    if (qty <= 0) {
      log_line = s.code + " sell signal but qty=0";
      return false;
    }
    double opx = order_price_for(s, q, false);
    if (opx <= 0) opx = q.price;
    double match_base = s.last_price;
    bool pop_match = stack_pair && !s.match_buy_prices.empty();
    if (pop_match) match_base = s.match_buy_prices.front();
    double sell_line = line_px(false);
    if (!wait_fill) opx = std::max(sell_line, q.price);

    std::string err;
    if (!emit_order(s, "sell", qty, opx, s.name.empty() ? q.name : s.name, "grid_sell", err)) {
      log_line = s.code + " write sell failed: " + err;
      return false;
    }

    if (wait_fill) {
      s.pending_side = "sell";
      s.pending_qty = qty;
      s.pending_price = opx;
      s.pending_ref_price = match_base;
      s.pending_grid_match_pop = pop_match;
      s.pending_grid_match_push = false;
      log_line = s.code + " SELL submitted " + std::to_string(qty) + " @ " + std::to_string(opx) + " await fill";
      return true;
    }

    s.pending_side = "sell";
    s.pending_qty = qty;
    s.pending_price = opx;
    s.pending_ref_price = match_base;
    s.pending_grid_match_pop = pop_match;
    s.pending_grid_match_push = false;
    apply_fill(s, q.price);
    log_line = s.code + " SELL " + std::to_string(qty) + " @ " + std::to_string(opx);
    if (!cfg_.write_orders) log_line += " (paper)";
    return true;
  }

  if (want_buy) {
    int qty = calc_buy_qty(s, q.price);
    if (qty <= 0) {
      log_line = s.code + " buy signal but qty=0";
      return false;
    }
    double opx = order_price_for(s, q, true);
    if (opx <= 0) opx = q.price;
    std::string err;
    std::string reason = s.margin_buy ? "grid_buy_margin" : "grid_buy";
    if (!emit_order(s, "buy", qty, opx, s.name.empty() ? q.name : s.name, reason, err)) {
      log_line = s.code + " write buy failed: " + err;
      return false;
    }

    double buy_line = line_px(true);
    if (!wait_fill) opx = std::min(buy_line > 0 ? buy_line : opx, q.price);
    const bool push_match = stack_pair;
    if (wait_fill) {
      s.pending_side = "buy";
      s.pending_qty = qty;
      s.pending_price = opx;
      s.pending_ref_price = s.last_price;
      s.pending_grid_match_pop = false;
      s.pending_grid_match_push = push_match;
      log_line = s.code + " BUY submitted " + std::to_string(qty) + " @ " + std::to_string(opx) + " await fill";
      return true;
    }

    s.pending_side = "buy";
    s.pending_qty = qty;
    s.pending_price = opx;
    s.pending_ref_price = s.last_price;
    s.pending_grid_match_push = push_match;
    apply_fill(s, q.price);
    log_line = s.code + " BUY " + std::to_string(qty) + " @ " + std::to_string(opx);
    if (!cfg_.write_orders) log_line += " (paper)";
    return true;
  }

  return false;
}

void GridEngine::tick(std::string& log_line) {
  log_line.clear();
  reload_from_disk();

  std::vector<std::string> codes;
  for (const auto& s : strategies_) {
    if (s.status == "running" || (s.status == "paused" && s.paused_by_bound)) codes.push_back(upper(s.code));
  }
  if (codes.empty()) {
    log_line = "no running strategies";
    return;
  }

  std::unordered_map<std::string, Quote> quotes;
  std::string err;
  std::string src = cfg_.quote_source.empty() ? "tencent" : cfg_.quote_source;
  if (!fetch_quotes(src, codes, quotes, err)) {
    log_line = "quote fail: " + err;
    return;
  }

  bool dirty = false;
  for (auto& s : strategies_) {
    if (s.status != "running" && !(s.status == "paused" && s.paused_by_bound)) continue;
    auto it = quotes.find(upper(s.code));
    if (it == quotes.end()) continue;
    if (s.name.empty() && !it->second.name.empty()) s.name = it->second.name;
    if (!cfg_.data_dir.empty()) {
      const auto sec = now_ms() / 1000;
      note_quote_bar(cfg_.data_dir, it->second, sec);
    }
    std::string line;
    if (eval_and_trade(s, it->second, line)) {
      dirty = true;
      if (!line.empty()) {
        if (!log_line.empty()) log_line += " | ";
        log_line += line;
      }
    } else if (!line.empty() &&
               (line.find("waiting") != std::string::npos || line.find("outside session") != std::string::npos)) {
      if (!log_line.empty()) log_line += " | ";
      log_line += line;
    }
  }
  if (dirty) {
    std::string serr;
    if (!save(serr)) {
      log_line += " | save fail: " + serr;
    }
  }
  if (log_line.empty()) {
    log_line = "heartbeat ok, quotes=" + std::to_string(quotes.size()) + " src=" + src;
  }
}

void GridEngine::run_loop(std::atomic<bool>& running) {
  while (running.load()) {
    std::string line;
    tick(line);
    std::cout << "[" << now_ms() << "] " << line << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.poll_ms));
  }
}

}  // namespace grid
