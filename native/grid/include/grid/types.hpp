#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace grid {

struct Strategy {
  std::string id;
  std::string code;
  std::string name;
  std::string status;  // running | paused | stopped

  double price_ceil = 0;
  double price_floor = 0;
  // pause | clear_pause | clear_stop
  std::string ceil_action = "pause";
  std::string floor_action = "clear_stop";

  double buy_qty = 100;
  std::string buy_unit = "shares";  // shares | amount
  double sell_qty = 0;
  std::string sell_unit = "shares";

  double last_price = 0;
  int last_qty = 0;
  bool use_order_price = true;
  bool use_cost_basis = false;
  bool auto_xrxd = true;

  bool by_price = false;
  double sell_up_pct = 2.0;
  double buy_down_pct = 2.0;

  double low_pivot = 0;
  double rebound_pct = 0;
  double high_pivot = 0;
  double pullback_pct = 0;
  int delay_confirm_sec = 0;
  bool check_buy_with_down = false;
  bool check_sell_with_up = false;

  int max_buy_streak = 0;
  int buy_streak = 0;
  double buy_qty_multiple = 0;  // pyramid: last_qty * multiple; 0 = use buy_qty
  bool cross_buy_multiply = false;
  bool cross_sell_multiply = false;
  bool sell_all_after_multi_buy = false;

  bool preorder_sell = false;
  bool preorder_buy = false;
  bool grid_match = false;
  bool check_fill = false;

  // touch = advance on trigger; fill = advance only after full fill at avg price
  std::string drive = "touch";
  // none | loose | strict. Locked after create.
  std::string pair_mode = "loose";
  int unpaired_limit = 0;
  bool pause_buy = false;
  bool pause_sell = false;
  double force_buy_qty = 0;
  double force_sell_qty = 0;
  bool gap_open_lots = false;
  int max_sell_streak = 0;
  int sell_streak = 0;
  int position_cap_shares = 0;
  bool stop_when_flat = false;
  double stop_price = 0;
  double double_add_price = 0;
  // none | pnl_pct | price
  std::string take_profit_mode = "none";
  double take_profit_value = 0;
  double rebuild_price = 0;
  // running | wait_rebuild
  std::string phase = "running";
  bool paused_by_bound = false;
  double prebury_band_pct = 1.5;
  std::string prebury_buy_id;
  std::string prebury_sell_id;
  double cost_basis = 0;
  int available_shares = -1;  // <0: unknown, use position_shares
  std::string day_key;
  std::string last_event;  // buy | sell
  int last_event_qty = 0;
  double last_event_price = 0;
  int burst_buy_lots = 1;   // runtime, not persisted
  int burst_sell_lots = 1;
  bool margin_buy = false;

  // assistant 16.1 extras
  bool session_enabled = true;
  std::string session_start = "09:25:00";
  std::string session_end = "15:00:00";
  // last | market | bid1..bid5 | ask1..ask5
  std::string buy_price_type = "ask1";
  std::string sell_price_type = "bid1";
  int price_offset_cents = 0;            // order price tweak in cents
  double max_position_amount = 0;        // 0 = unlimited
  int reserve_base_shares = 0;           // keep base shares on sell
  int position_shares = 0;               // estimated position (engine-owned)
  std::vector<double> match_buy_prices;  // grid_match buy stack

  double range_pnl = 0;
  double range_save = 0;
  std::string note;

  int64_t pivot_touch_ms = 0;
  bool sell_armed = false;
  bool buy_armed = false;
  std::string pending_order_id;
  // check_fill: defer grid state until ack/done
  std::string pending_side;     // buy | sell
  int pending_qty = 0;
  double pending_price = 0;
  double pending_ref_price = 0;  // last_price / match buy for pnl
  bool pending_grid_match_pop = false;
  bool pending_grid_match_push = false;
};

struct Quote {
  std::string code;
  std::string name;
  double price = 0;
  double prev_close = 0;
  double open = 0;
  double high = 0;
  double low = 0;
  double volume = 0;
  double amount = 0;
  double bid = 0;   // = bids[0]
  double ask = 0;   // = asks[0]
  double bids[5] = {};  // 买一..买五
  double asks[5] = {};  // 卖一..卖五
  double bid_vols[5] = {};
  double ask_vols[5] = {};
};

struct OrderIntent {
  std::string client_order_id;
  std::string code;
  std::string side;  // buy | sell
  int qty = 0;
  std::string price_type = "limit";
  double price = 0;
  std::string account_id = "default";
  std::string name;
  std::string reason;
  bool margin_buy = false;
};

struct EngineConfig {
  std::filesystem::path data_dir;
  std::filesystem::path grid_json;
  std::filesystem::path orders_dir;
  std::string quote_source = "tencent";
  std::string broker_channel = "paper";
  int poll_ms = 3000;
  bool dry_run = false;
  bool write_orders = true;
  bool replay = false;  // backtest: ignore session clock
};

}  // namespace grid
