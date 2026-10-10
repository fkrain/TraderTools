#include "desk/app_state.hpp"

#include "desk/settings.hpp"
#include "grid/backtest.hpp"
#include "grid/json_io.hpp"
#include "grid/quote_factory.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>
#include <random>

using json = nlohmann::json;

namespace desk {
namespace {

std::string random_id() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(0, 15);
  const char* hex = "0123456789abcdef";
  std::string s(10, '0');
  for (char& c : s) c = hex[dist(rng)];
  return s;
}

double now_sec() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() / 1000.0;
}

json strategy_to_json(const grid::Strategy& s) {
  return json{
      {"id", s.id},
      {"code", s.code},
      {"name", s.name},
      {"status", s.status},
      {"price_ceil", s.price_ceil},
      {"price_floor", s.price_floor},
      {"ceil_action", s.ceil_action},
      {"floor_action", s.floor_action},
      {"buy_qty", s.buy_qty},
      {"buy_unit", s.buy_unit},
      {"sell_qty", s.sell_qty},
      {"sell_unit", s.sell_unit},
      {"last_price", s.last_price},
      {"last_qty", s.last_qty},
      {"use_order_price", s.use_order_price},
      {"use_cost_basis", s.use_cost_basis},
      {"auto_xrxd", s.auto_xrxd},
      {"by_price", s.by_price},
      {"sell_up_pct", s.sell_up_pct},
      {"buy_down_pct", s.buy_down_pct},
      {"low_pivot", s.low_pivot},
      {"rebound_pct", s.rebound_pct},
      {"high_pivot", s.high_pivot},
      {"pullback_pct", s.pullback_pct},
      {"delay_confirm_sec", s.delay_confirm_sec},
      {"check_buy_with_down", s.check_buy_with_down},
      {"check_sell_with_up", s.check_sell_with_up},
      {"max_buy_streak", s.max_buy_streak},
      {"buy_streak", s.buy_streak},
      {"buy_qty_multiple", s.buy_qty_multiple},
      {"cross_buy_multiply", s.cross_buy_multiply},
      {"cross_sell_multiply", s.cross_sell_multiply},
      {"sell_all_after_multi_buy", s.sell_all_after_multi_buy},
      {"preorder_sell", s.preorder_sell},
      {"preorder_buy", s.preorder_buy},
      {"grid_match", s.grid_match},
      {"check_fill", s.check_fill},
      {"drive", s.drive},
      {"pair_mode", s.pair_mode},
      {"unpaired_limit", s.unpaired_limit},
      {"pause_buy", s.pause_buy},
      {"pause_sell", s.pause_sell},
      {"force_buy_qty", s.force_buy_qty},
      {"force_sell_qty", s.force_sell_qty},
      {"gap_open_lots", s.gap_open_lots},
      {"max_sell_streak", s.max_sell_streak},
      {"sell_streak", s.sell_streak},
      {"position_cap_shares", s.position_cap_shares},
      {"stop_when_flat", s.stop_when_flat},
      {"stop_price", s.stop_price},
      {"double_add_price", s.double_add_price},
      {"take_profit_mode", s.take_profit_mode},
      {"take_profit_value", s.take_profit_value},
      {"rebuild_price", s.rebuild_price},
      {"phase", s.phase},
      {"paused_by_bound", s.paused_by_bound},
      {"prebury_band_pct", s.prebury_band_pct},
      {"cost_basis", s.cost_basis},
      {"available_shares", s.available_shares},
      {"margin_buy", s.margin_buy},
      {"session_enabled", s.session_enabled},
      {"session_start", s.session_start},
      {"session_end", s.session_end},
      {"buy_price_type", s.buy_price_type},
      {"sell_price_type", s.sell_price_type},
      {"price_offset_cents", s.price_offset_cents},
      {"max_position_amount", s.max_position_amount},
      {"reserve_base_shares", s.reserve_base_shares},
      {"position_shares", s.position_shares},
      {"match_buy_prices", s.match_buy_prices},
      {"range_pnl", s.range_pnl},
      {"range_save", s.range_save},
      {"note", s.note},
  };
}

void patch_strategy(grid::Strategy& s, const json& j) {
  if (j.contains("code")) s.code = j.value("code", s.code);
  if (j.contains("name")) s.name = j.value("name", s.name);
  if (j.contains("status")) s.status = j.value("status", s.status);
  if (j.contains("price_ceil")) s.price_ceil = j.value("price_ceil", s.price_ceil);
  if (j.contains("price_floor")) s.price_floor = j.value("price_floor", s.price_floor);
  if (j.contains("ceil_action")) s.ceil_action = j.value("ceil_action", s.ceil_action);
  if (j.contains("floor_action")) s.floor_action = j.value("floor_action", s.floor_action);
  if (j.contains("buy_qty")) s.buy_qty = j.value("buy_qty", s.buy_qty);
  if (j.contains("buy_unit")) s.buy_unit = j.value("buy_unit", s.buy_unit);
  if (j.contains("sell_qty")) s.sell_qty = j.value("sell_qty", s.sell_qty);
  if (j.contains("sell_unit")) s.sell_unit = j.value("sell_unit", s.sell_unit);
  if (j.contains("last_price")) s.last_price = j.value("last_price", s.last_price);
  if (j.contains("last_qty")) s.last_qty = j.value("last_qty", s.last_qty);
  if (j.contains("use_order_price")) s.use_order_price = j.value("use_order_price", s.use_order_price);
  if (j.contains("use_cost_basis")) s.use_cost_basis = j.value("use_cost_basis", s.use_cost_basis);
  if (j.contains("auto_xrxd")) s.auto_xrxd = j.value("auto_xrxd", s.auto_xrxd);
  if (j.contains("by_price")) s.by_price = j.value("by_price", s.by_price);
  if (j.contains("sell_up_pct")) s.sell_up_pct = j.value("sell_up_pct", s.sell_up_pct);
  if (j.contains("buy_down_pct")) s.buy_down_pct = j.value("buy_down_pct", s.buy_down_pct);
  if (j.contains("low_pivot")) s.low_pivot = j.value("low_pivot", s.low_pivot);
  if (j.contains("rebound_pct")) s.rebound_pct = j.value("rebound_pct", s.rebound_pct);
  if (j.contains("high_pivot")) s.high_pivot = j.value("high_pivot", s.high_pivot);
  if (j.contains("pullback_pct")) s.pullback_pct = j.value("pullback_pct", s.pullback_pct);
  if (j.contains("delay_confirm_sec")) s.delay_confirm_sec = j.value("delay_confirm_sec", s.delay_confirm_sec);
  if (j.contains("check_buy_with_down")) s.check_buy_with_down = j.value("check_buy_with_down", s.check_buy_with_down);
  if (j.contains("check_sell_with_up")) s.check_sell_with_up = j.value("check_sell_with_up", s.check_sell_with_up);
  if (j.contains("max_buy_streak")) s.max_buy_streak = j.value("max_buy_streak", s.max_buy_streak);
  if (j.contains("buy_streak")) s.buy_streak = j.value("buy_streak", s.buy_streak);
  if (j.contains("buy_qty_multiple")) s.buy_qty_multiple = j.value("buy_qty_multiple", s.buy_qty_multiple);
  if (j.contains("cross_buy_multiply")) s.cross_buy_multiply = j.value("cross_buy_multiply", s.cross_buy_multiply);
  if (j.contains("cross_sell_multiply")) s.cross_sell_multiply = j.value("cross_sell_multiply", s.cross_sell_multiply);
  if (j.contains("sell_all_after_multi_buy")) s.sell_all_after_multi_buy = j.value("sell_all_after_multi_buy", s.sell_all_after_multi_buy);
  if (j.contains("preorder_sell")) s.preorder_sell = j.value("preorder_sell", s.preorder_sell);
  if (j.contains("preorder_buy")) s.preorder_buy = j.value("preorder_buy", s.preorder_buy);
  if (j.contains("grid_match")) s.grid_match = j.value("grid_match", s.grid_match);
  if (j.contains("check_fill")) s.check_fill = j.value("check_fill", s.check_fill);
  if (j.contains("drive")) s.drive = j.value("drive", s.drive);
  if (j.contains("pair_mode")) s.pair_mode = j.value("pair_mode", s.pair_mode);
  if (j.contains("unpaired_limit")) s.unpaired_limit = j.value("unpaired_limit", s.unpaired_limit);
  if (j.contains("pause_buy")) s.pause_buy = j.value("pause_buy", s.pause_buy);
  if (j.contains("pause_sell")) s.pause_sell = j.value("pause_sell", s.pause_sell);
  if (j.contains("force_buy_qty")) s.force_buy_qty = j.value("force_buy_qty", s.force_buy_qty);
  if (j.contains("force_sell_qty")) s.force_sell_qty = j.value("force_sell_qty", s.force_sell_qty);
  if (j.contains("gap_open_lots")) s.gap_open_lots = j.value("gap_open_lots", s.gap_open_lots);
  if (j.contains("max_sell_streak")) s.max_sell_streak = j.value("max_sell_streak", s.max_sell_streak);
  if (j.contains("position_cap_shares")) s.position_cap_shares = j.value("position_cap_shares", s.position_cap_shares);
  if (j.contains("stop_when_flat")) s.stop_when_flat = j.value("stop_when_flat", s.stop_when_flat);
  if (j.contains("stop_price")) s.stop_price = j.value("stop_price", s.stop_price);
  if (j.contains("double_add_price")) s.double_add_price = j.value("double_add_price", s.double_add_price);
  if (j.contains("take_profit_mode")) s.take_profit_mode = j.value("take_profit_mode", s.take_profit_mode);
  if (j.contains("take_profit_value")) s.take_profit_value = j.value("take_profit_value", s.take_profit_value);
  if (j.contains("rebuild_price")) s.rebuild_price = j.value("rebuild_price", s.rebuild_price);
  if (j.contains("prebury_band_pct")) s.prebury_band_pct = j.value("prebury_band_pct", s.prebury_band_pct);
  if (s.drive == "fill") s.check_fill = true;
  if (s.drive == "touch") s.check_fill = false;
  if (s.pair_mode == "strict") s.grid_match = true;
  if (s.pair_mode == "none" || s.pair_mode == "loose") s.grid_match = false;
  if (j.contains("margin_buy")) s.margin_buy = j.value("margin_buy", s.margin_buy);
  if (j.contains("session_enabled")) s.session_enabled = j.value("session_enabled", s.session_enabled);
  if (j.contains("session_start")) s.session_start = j.value("session_start", s.session_start);
  if (j.contains("session_end")) s.session_end = j.value("session_end", s.session_end);
  if (j.contains("buy_price_type")) s.buy_price_type = j.value("buy_price_type", s.buy_price_type);
  if (j.contains("sell_price_type")) s.sell_price_type = j.value("sell_price_type", s.sell_price_type);
  if (j.contains("price_offset_cents")) s.price_offset_cents = j.value("price_offset_cents", s.price_offset_cents);
  if (j.contains("max_position_amount")) s.max_position_amount = j.value("max_position_amount", s.max_position_amount);
  if (j.contains("reserve_base_shares")) s.reserve_base_shares = j.value("reserve_base_shares", s.reserve_base_shares);
  if (j.contains("position_shares")) s.position_shares = j.value("position_shares", s.position_shares);
  if (j.contains("match_buy_prices") && j["match_buy_prices"].is_array()) {
    s.match_buy_prices.clear();
    for (const auto& v : j["match_buy_prices"]) {
      if (v.is_number()) s.match_buy_prices.push_back(v.get<double>());
    }
  }
  if (j.contains("range_pnl")) s.range_pnl = j.value("range_pnl", s.range_pnl);
  if (j.contains("range_save")) s.range_save = j.value("range_save", s.range_save);
  if (j.contains("note")) s.note = j.value("note", s.note);
}

}  // namespace

AppState& AppState::instance() {
  static AppState s;
  return s;
}

void AppState::init(const std::filesystem::path& root_dir) {
  root_ = root_dir;
  data_dir_ = root_dir / "data";
  web_dir_ = root_dir / "web";
  grid_json_ = data_dir_ / "grid.json";
  orders_dir_ = data_dir_ / "orders";
  std::filesystem::create_directories(data_dir_);
  std::filesystem::create_directories(orders_dir_);
  auto ord = SettingsStore::instance().orders_root();
  auto orders = ord.empty() ? orders_dir_ : ord;
  products_.init(data_dir_, orders);
  modules_.init(data_dir_, orders);
  workflows_.init(data_dir_);
  if (!product_running_.load()) {
    product_running_ = true;
    product_thr_ = std::thread([this] { product_loop(); });
    append_log("[product] background monitor started");
  }
}

void AppState::shutdown() {
  product_running_ = false;
  if (product_thr_.joinable()) product_thr_.join();
  engine_stop();
}

void AppState::product_loop() {
  while (product_running_.load()) {
    std::string line;
    try {
      products_.tick(line);
      std::string line2;
      modules_.tick(
          [this](const std::string& code, const std::string& side, int qty, double price, const std::string& reason,
                 const std::string& name, std::string& err, const std::string& price_type) {
            return products_.place_order(code, side, qty, price, reason, name, err, price_type);
          },
          [this](const std::string& kind, const std::string& msg) { products_.note_event(kind, msg); }, line2);
      if (!line2.empty()) {
        if (!line.empty()) line += " | ";
        line += line2;
      }
      std::string line3;
      workflows_.tick(
          [this](const std::string& code, const std::string& side, int qty, double price, const std::string& reason,
                 const std::string& name, std::string& err, const std::string& price_type) {
            return products_.place_order(code, side, qty, price, reason, name, err, price_type);
          },
          [this](const std::string& kind, const std::string& msg) { products_.note_event(kind, msg); }, line3);
      if (!line3.empty() && line3.find("heartbeat") == std::string::npos) {
        if (!line.empty()) line += " | ";
        line += line3;
      }
      if (!line.empty() && line.find("heartbeat") == std::string::npos && line.find("idle:") == std::string::npos)
        append_log("[product] " + line);
    } catch (...) {
      append_log("[product] tick exception");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  }
}

void AppState::append_log(const std::string& line) {
  std::lock_guard<std::mutex> lock(mu_);
  logs_.push_back(line);
  if (logs_.size() > 200) logs_.erase(logs_.begin(), logs_.begin() + (logs_.size() - 200));
}

std::vector<std::string> AppState::log_tail(size_t n) {
  std::lock_guard<std::mutex> lock(mu_);
  if (logs_.size() <= n) return logs_;
  return {logs_.end() - (std::ptrdiff_t)n, logs_.end()};
}

std::string AppState::grid_list_json() {
  std::vector<grid::Strategy> items;
  std::string err;
  if (!grid::load_strategies(grid_json_, items, err)) {
    items.clear();
  }
  json arr = json::array();
  double pnl = 0, save = 0;
  int running = 0;
  for (const auto& s : items) {
    arr.push_back(strategy_to_json(s));
    pnl += s.range_pnl;
    save += s.range_save;
    if (s.status == "running") ++running;
  }
  json root = {
      {"items", arr},
      {"summary", {{"count", items.size()}, {"running", running}, {"range_pnl", pnl}, {"range_save", save}}},
  };
  return root.dump();
}

bool AppState::grid_create(const std::string& body_json, std::string& out_json, std::string& err) {
  json body;
  try {
    body = json::parse(body_json);
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
  std::vector<grid::Strategy> items;
  if (!grid::load_strategies(grid_json_, items, err)) {
    items.clear();
    err.clear();
  }
  grid::Strategy s;
  s.id = random_id();
  s.status = "paused";
  patch_strategy(s, body);
  if (s.code.empty()) {
    err = "code required";
    return false;
  }
  items.push_back(s);
  if (!grid::save_strategies(grid_json_, items, err)) return false;
  out_json = strategy_to_json(s).dump();
  return true;
}

bool AppState::grid_update(const std::string& id, const std::string& body_json, std::string& out_json, std::string& err) {
  json body;
  try {
    body = json::parse(body_json.empty() ? "{}" : body_json);
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
  std::vector<grid::Strategy> items;
  if (!grid::load_strategies(grid_json_, items, err)) return false;
  for (auto& s : items) {
    if (s.id != id) continue;
    const std::string old_drive = s.drive;
    const std::string old_pair = s.pair_mode;
    if (body.contains("drive") && !old_drive.empty() && body.value("drive", old_drive) != old_drive) {
      err = "驱动方式创建后不可改";
      return false;
    }
    if (body.contains("pair_mode") && !old_pair.empty() && body.value("pair_mode", old_pair) != old_pair) {
      err = "配对方式创建后不可改";
      return false;
    }
    patch_strategy(s, body);
    if (!grid::save_strategies(grid_json_, items, err)) return false;
    out_json = strategy_to_json(s).dump();
    return true;
  }
  err = "not found";
  return false;
}

bool AppState::grid_delete(const std::string& id, std::string& err) {
  std::vector<grid::Strategy> items;
  if (!grid::load_strategies(grid_json_, items, err)) return false;
  auto before = items.size();
  items.erase(std::remove_if(items.begin(), items.end(), [&](const grid::Strategy& s) { return s.id == id; }),
              items.end());
  if (items.size() == before) {
    err = "not found";
    return false;
  }
  return grid::save_strategies(grid_json_, items, err);
}

bool AppState::grid_toggle(const std::string& id, std::string& out_json, std::string& err) {
  std::vector<grid::Strategy> items;
  if (!grid::load_strategies(grid_json_, items, err)) return false;
  for (auto& s : items) {
    if (s.id != id) continue;
    s.status = (s.status == "running") ? "paused" : "running";
    if (!grid::save_strategies(grid_json_, items, err)) return false;
    out_json = strategy_to_json(s).dump();
    return true;
  }
  err = "not found";
  return false;
}

bool AppState::grid_backtest(const std::string& body_json, std::string& out_json, std::string& err) {
  json body;
  try {
    body = json::parse(body_json.empty() ? "{}" : body_json);
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
  grid::Strategy s;
  s.status = "running";
  if (body.contains("strategy") && body["strategy"].is_object()) patch_strategy(s, body["strategy"]);
  else patch_strategy(s, body);
  if (body.contains("id")) {
    std::vector<grid::Strategy> items;
    if (grid::load_strategies(grid_json_, items, err)) {
      for (const auto& item : items) {
        if (item.id == body.value("id", "")) {
          s = item;
          break;
        }
      }
    }
    err.clear();
  }
  if (!body.contains("bars") || !body["bars"].is_array() || body["bars"].empty()) {
    err = "bars required";
    return false;
  }
  std::vector<grid::MinuteBar> bars;
  for (const auto& b : body["bars"]) {
    grid::MinuteBar m;
    m.open = b.value("open", b.value("o", 0.0));
    m.high = b.value("high", b.value("h", 0.0));
    m.low = b.value("low", b.value("l", 0.0));
    m.close = b.value("close", b.value("c", 0.0));
    bars.push_back(m);
  }
  s.session_enabled = false;
  auto report = grid::backtest_grid(s, bars, body.value("cash", 100000.0), body.value("commission", 0.00025),
                                    body.value("stamp", 0.0005));
  out_json = report.dump();
  return true;
}

std::string AppState::engine_status_json() {
  auto settings = SettingsStore::instance().load();
  json j = {
      {"running", running_.load()},
      {"pid", 0},
      {"exit_code", nullptr},
      {"exe", "embedded"},
      {"exe_found", true},
      {"dry_run", dry_run_},
      {"poll_ms", poll_ms_},
      {"started_at", started_at_},
      {"quote_source", settings.quote_source},
      {"broker_channel", settings.broker_channel},
      {"log_tail", log_tail(40)},
      {"host", "cpp-desktop"},
  };
  return j.dump();
}

bool AppState::engine_start(bool dry_run, int poll_ms, std::string& err) {
  if (running_.load()) return true;

  auto settings = SettingsStore::instance().load();
  grid::EngineConfig cfg;
  cfg.data_dir = data_dir_;
  cfg.grid_json = grid_json_;
  cfg.orders_dir = SettingsStore::instance().orders_root();
  cfg.quote_source = settings.quote_source.empty() ? "tencent" : settings.quote_source;
  cfg.broker_channel = settings.broker_channel.empty() ? "paper" : settings.broker_channel;
  cfg.poll_ms = poll_ms > 500 ? poll_ms : 3000;
  cfg.dry_run = dry_run;
  cfg.write_orders = !dry_run && cfg.broker_channel != "paper";

  auto eng = std::make_unique<grid::GridEngine>(cfg);
  if (!eng->load(err)) return false;

  dry_run_ = dry_run;
  poll_ms_ = cfg.poll_ms;
  started_at_ = now_sec();
  engine_ = std::move(eng);
  running_ = true;
  append_log("[start] quote=" + cfg.quote_source + " dry_run=" + (dry_run ? "1" : "0") +
             " broker=" + cfg.broker_channel + " write_orders=" + (cfg.write_orders ? "1" : "0"));
  thr_ = std::thread([this] { engine_loop(); });
  return true;
}

void AppState::engine_stop() {
  if (!running_.load()) return;
  running_ = false;
  if (thr_.joinable()) thr_.join();
  {
    std::lock_guard<std::mutex> lock(mu_);
    engine_.reset();
  }
  append_log("[stop] engine stopped");
}

void AppState::engine_loop() {
  while (running_.load()) {
    std::string line;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (engine_) engine_->tick(line);
    }
    if (!line.empty()) append_log(line);
    for (int i = 0; i < poll_ms_ / 50 && running_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
}

}  // namespace desk
