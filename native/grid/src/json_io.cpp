#include "grid/json_io.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace grid {
namespace {

Strategy from_json(const json& j) {
  Strategy s;
  s.id = j.value("id", "");
  s.code = j.value("code", "");
  s.name = j.value("name", "");
  s.status = j.value("status", "paused");
  s.price_ceil = j.value("price_ceil", 0.0);
  s.price_floor = j.value("price_floor", 0.0);
  s.ceil_action = j.value("ceil_action", "pause");
  s.floor_action = j.value("floor_action", "clear_stop");
  s.buy_qty = j.value("buy_qty", 100.0);
  s.buy_unit = j.value("buy_unit", "shares");
  s.sell_qty = j.value("sell_qty", 0.0);
  s.sell_unit = j.value("sell_unit", "shares");
  s.last_price = j.value("last_price", 0.0);
  s.last_qty = j.value("last_qty", 0);
  s.use_order_price = j.value("use_order_price", true);
  s.use_cost_basis = j.value("use_cost_basis", false);
  s.auto_xrxd = j.value("auto_xrxd", true);
  s.by_price = j.value("by_price", false);
  s.sell_up_pct = j.value("sell_up_pct", 2.0);
  s.buy_down_pct = j.value("buy_down_pct", 2.0);
  s.low_pivot = j.value("low_pivot", 0.0);
  s.rebound_pct = j.value("rebound_pct", 0.0);
  s.high_pivot = j.value("high_pivot", 0.0);
  s.pullback_pct = j.value("pullback_pct", 0.0);
  s.delay_confirm_sec = j.value("delay_confirm_sec", 0);
  s.check_buy_with_down = j.value("check_buy_with_down", false);
  s.check_sell_with_up = j.value("check_sell_with_up", false);
  s.max_buy_streak = j.value("max_buy_streak", 0);
  s.buy_streak = j.value("buy_streak", 0);
  s.buy_qty_multiple = j.value("buy_qty_multiple", 0.0);
  s.cross_buy_multiply = j.value("cross_buy_multiply", false);
  s.cross_sell_multiply = j.value("cross_sell_multiply", false);
  s.sell_all_after_multi_buy = j.value("sell_all_after_multi_buy", false);
  s.preorder_sell = j.value("preorder_sell", false);
  s.preorder_buy = j.value("preorder_buy", false);
  s.grid_match = j.value("grid_match", false);
  s.check_fill = j.value("check_fill", false);
  s.drive = j.value("drive", "");
  s.pair_mode = j.value("pair_mode", "");
  if (s.drive.empty()) s.drive = s.check_fill ? "fill" : "touch";
  if (s.pair_mode.empty()) s.pair_mode = s.grid_match ? "strict" : "loose";
  if (s.pair_mode != "none" && s.pair_mode != "loose" && s.pair_mode != "strict") s.pair_mode = "loose";
  if (s.drive != "touch" && s.drive != "fill") s.drive = "touch";
  s.unpaired_limit = j.value("unpaired_limit", 0);
  s.pause_buy = j.value("pause_buy", false);
  s.pause_sell = j.value("pause_sell", false);
  s.force_buy_qty = j.value("force_buy_qty", 0.0);
  s.force_sell_qty = j.value("force_sell_qty", 0.0);
  s.gap_open_lots = j.value("gap_open_lots", false);
  s.max_sell_streak = j.value("max_sell_streak", 0);
  s.sell_streak = j.value("sell_streak", 0);
  s.position_cap_shares = j.value("position_cap_shares", 0);
  s.stop_when_flat = j.value("stop_when_flat", false);
  s.stop_price = j.value("stop_price", 0.0);
  s.double_add_price = j.value("double_add_price", 0.0);
  s.take_profit_mode = j.value("take_profit_mode", "none");
  s.take_profit_value = j.value("take_profit_value", 0.0);
  s.rebuild_price = j.value("rebuild_price", 0.0);
  s.phase = j.value("phase", "running");
  s.paused_by_bound = j.value("paused_by_bound", false);
  s.prebury_band_pct = j.value("prebury_band_pct", 1.5);
  s.prebury_buy_id = j.value("prebury_buy_id", "");
  s.prebury_sell_id = j.value("prebury_sell_id", "");
  s.cost_basis = j.value("cost_basis", 0.0);
  s.available_shares = j.value("available_shares", -1);
  s.day_key = j.value("day_key", "");
  s.margin_buy = j.value("margin_buy", false);
  s.session_enabled = j.value("session_enabled", true);
  s.session_start = j.value("session_start", "09:25:00");
  s.session_end = j.value("session_end", "15:00:00");
  s.buy_price_type = j.value("buy_price_type", "ask1");
  s.sell_price_type = j.value("sell_price_type", "bid1");
  s.price_offset_cents = j.value("price_offset_cents", 0);
  s.max_position_amount = j.value("max_position_amount", 0.0);
  s.reserve_base_shares = j.value("reserve_base_shares", 0);
  s.position_shares = j.value("position_shares", 0);
  s.range_pnl = j.value("range_pnl", 0.0);
  s.range_save = j.value("range_save", 0.0);
  s.note = j.value("note", "");
  s.pending_order_id = j.value("pending_order_id", "");
  s.pending_side = j.value("pending_side", "");
  s.pending_qty = j.value("pending_qty", 0);
  s.pending_price = j.value("pending_price", 0.0);
  s.pending_ref_price = j.value("pending_ref_price", 0.0);
  s.pending_grid_match_pop = j.value("pending_grid_match_pop", false);
  s.pending_grid_match_push = j.value("pending_grid_match_push", false);
  const auto& stack = j.contains("match_buy_prices") ? j["match_buy_prices"]
                      : j.contains("buy_match_stack")  ? j["buy_match_stack"]
                                                       : json::array();
  if (stack.is_array()) {
    for (const auto& v : stack) {
      if (v.is_number()) s.match_buy_prices.push_back(v.get<double>());
    }
  }
  return s;
}

json to_json(const Strategy& s) {
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
      {"prebury_buy_id", s.prebury_buy_id},
      {"prebury_sell_id", s.prebury_sell_id},
      {"cost_basis", s.cost_basis},
      {"available_shares", s.available_shares},
      {"day_key", s.day_key},
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
      {"pending_order_id", s.pending_order_id},
      {"pending_side", s.pending_side},
      {"pending_qty", s.pending_qty},
      {"pending_price", s.pending_price},
      {"pending_ref_price", s.pending_ref_price},
      {"pending_grid_match_pop", s.pending_grid_match_pop},
      {"pending_grid_match_push", s.pending_grid_match_push},
  };
}

}  // namespace

bool load_strategies(const std::filesystem::path& path, std::vector<Strategy>& out, std::string& err) {
  std::ifstream in(path);
  if (!in) {
    err = "cannot open " + path.u8string();
    return false;
  }
  json root;
  try {
    in >> root;
  } catch (const std::exception& ex) {
    err = std::string("parse grid.json failed: ") + ex.what();
    return false;
  }
  if (!root.is_array()) {
    err = "grid.json root must be array";
    return false;
  }
  out.clear();
  out.reserve(root.size());
  for (const auto& item : root) {
    out.push_back(from_json(item));
  }
  return true;
}

bool save_strategies(const std::filesystem::path& path, const std::vector<Strategy>& items, std::string& err) {
  json root = json::array();
  for (const auto& s : items) {
    root.push_back(to_json(s));
  }
  const std::filesystem::path tmp = [&] {
    auto t = path;
    t += ".tmp";
    return t;
  }();
  {
    std::ofstream out(tmp, std::ios::binary);
    if (!out) {
      err = "cannot write " + tmp.u8string();
      return false;
    }
    out << root.dump(2);
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    err = "replace grid.json failed: " + ec.message();
    return false;
  }
  return true;
}

}  // namespace grid
