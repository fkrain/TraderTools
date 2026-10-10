#pragma once

#include "grid/types.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace desk {

struct AlertRule {
  std::string id;
  std::string name;
  std::string source = "price";  // price | file | pct
  std::string code;              // for price/pct
  std::string sign_name;         // for file source (tdx-like)
  std::string side = "buy";      // buy | sell | notify
  std::string compare = "gte";   // gte | lte
  double trigger_price = 0;
  double trigger_pct = 0;        // pct mode vs ref_price
  double ref_price = 0;
  double qty = 100;
  std::string qty_unit = "shares";  // shares | amount
  bool auto_trade = true;
  bool once = false;
  int cooldown_sec = 60;
  bool enabled = true;
  std::string status = "watching";  // watching | triggered | paused | disabled
  std::string last_trigger_at;
  int trigger_count = 0;
  int64_t last_trigger_ms = 0;
  std::string note;
};

struct StopRule {
  std::string id;
  std::string code;
  std::string name;
  double cost_price = 0;
  double stop_pct = 5;   // sell if drop
  double take_pct = 10;  // sell if rise
  double stop_price = 0;
  double take_price = 0;
  bool trail_enabled = false;
  double trail_pct = 0;
  double high_water = 0;
  int qty = 100;
  bool enabled = true;
  std::string status = "watching";
  std::string last_trigger_at;
  std::string note;
};

struct CondOrder {
  std::string id;
  std::string code;
  std::string name;
  std::string side = "buy";
  std::string kind = "price";  // price | time | pct
  std::string compare = "gte";
  double trigger_price = 0;
  double trigger_pct = 0;
  std::string trigger_time;
  double day_anchor = 0;
  int qty = 100;
  std::string price_type = "last";  // market|last|bid1..5|ask1..5
  double limit_price = 0;           // >0 overrides price_type
  bool enabled = true;
  std::string status = "armed";  // armed | filled | cancelled
  std::string last_trigger_at;
  std::string note;
};

struct WatchItem {
  std::string id;
  std::string code;
  std::string name;
  std::string tag;
  std::string note;
  double alert_high = 0;
  double alert_low = 0;
};

struct RiskConfig {
  bool trade_days_only = true;
  bool session_only = true;
  std::string session_start = "09:25:00";
  std::string session_end = "15:00:00";
  // MVP hard limits — persisted but always enforced when >0
  int max_orders_per_day = 20;
  double max_order_amount = 500000;   // 单笔上限（元）
  double max_daily_loss = 50000;      // 日亏上限（元），触发后当日暂停
  int code_cooldown_sec = 60;         // 同代码冷却
  bool confirm_before_trade = true;   // 半自动：先待确认
  bool sound_on_trigger = true;
  bool notify_log = true;
  std::string tdx_sign_file;  // file or directory
  double sponsor_equity_limit = 1000000;  // soft remind
};

struct RiskState {
  std::string day;  // YYYYMMDD
  int orders_today = 0;
  double day_start_equity = 0;
  bool paused_loss = false;
  std::string sponsor_reminded_day;
};

struct PendingConfirm {
  std::string id;
  std::string code;
  std::string name;
  std::string side;
  int qty = 0;
  double price = 0;
  std::string price_type = "limit";
  std::string reason;
  std::string created_at;
};

struct T0Rule {
  std::string id;
  std::string code;
  std::string name;
  double buy_down_pct = 1.0;
  double sell_up_pct = 1.0;
  int buy_qty = 100;
  int sell_qty = 0;  // 0 = same as buy_qty
  int qty = 100;     // legacy alias of buy_qty
  double base_price = 0;   // user baseline; 0 = auto first quote each day
  double day_anchor = 0;   // active anchor for triggers
  std::string order_type = "market";  // market|last|bid1..5|ask1..5
  bool long_today = false;
  bool enabled = true;
  bool session_only = true;
  int max_rounds = 0;  // 0 = unlimited buy-sell cycles per day
  int rounds_today = 0;
  std::string day_key;  // YYYYMMDD for daily reset
  int cooldown_sec = 3;
  int64_t last_action_ms = 0;
  std::string status = "watching";
  std::string last_trigger_at;
  std::string note;
};

struct BasketLeg {
  std::string code;
  std::string side = "buy";
  int qty = 100;
  double weight = 0;  // optional, if >0 allocate by weight*capital
};

struct Basket {
  std::string id;
  std::string name;
  double capital = 0;
  std::vector<BasketLeg> legs;
  std::string note;
};

struct BoardRule {
  std::string id;
  std::string code;
  std::string name;
  double seal_pct = 9.5;  // trigger when day change >=
  int qty = 100;
  bool enabled = true;
  std::string status = "watching";
  std::string last_trigger_at;
  std::string day_fired;  // YYYYMMDD once per day
};

struct FormulaJob {
  std::string id;
  std::string name;       // formula / sign label
  std::string side = "buy";
  double qty = 100;
  std::string qty_unit = "shares";
  bool enabled = true;
  std::string status = "watching";
  std::string last_trigger_at;
  int trigger_count = 0;
};

struct Account {
  std::string id;
  std::string label;
  std::string broker_channel = "paper";
  double capital = 1000000;
  double cash = 1000000;
  double equity = 1000000;
  bool enabled = true;
  std::string note;
};

struct CurvePoint {
  std::string day;
  double equity = 0;
  double cash = 0;
};

nlohmann::json alert_to_json(const AlertRule& a);
AlertRule alert_from_json(const nlohmann::json& j);
nlohmann::json stop_to_json(const StopRule& s);
StopRule stop_from_json(const nlohmann::json& j);
nlohmann::json cond_to_json(const CondOrder& c);
CondOrder cond_from_json(const nlohmann::json& j);
nlohmann::json watch_to_json(const WatchItem& w);
WatchItem watch_from_json(const nlohmann::json& j);
nlohmann::json risk_to_json(const RiskConfig& r);
RiskConfig risk_from_json(const nlohmann::json& j);
nlohmann::json pending_to_json(const PendingConfirm& p);

bool load_json_array(const std::filesystem::path& path, nlohmann::json& out, std::string& err);
bool save_json_array(const std::filesystem::path& path, const nlohmann::json& arr, std::string& err);
bool load_json_object(const std::filesystem::path& path, nlohmann::json& out, std::string& err);
bool save_json_object(const std::filesystem::path& path, const nlohmann::json& obj, std::string& err);

std::string new_id();
std::string now_str();
int64_t now_ms();
bool is_a_share_trading_day();
void set_holiday_file(const std::filesystem::path& path);
bool in_session_hms(const std::string& start, const std::string& end);
int floor_lot(int qty, const std::string& code);
std::string today_ymd();
bool safe_backup_file(const std::filesystem::path& path);
double quote_price_by_type(const grid::Quote& q, const std::string& type);
std::string order_price_type_for(const std::string& quote_type);  // market|limit

nlohmann::json t0_to_json(const T0Rule& r);
T0Rule t0_from_json(const nlohmann::json& j);
nlohmann::json basket_to_json(const Basket& b);
Basket basket_from_json(const nlohmann::json& j);
nlohmann::json board_to_json(const BoardRule& b);
BoardRule board_from_json(const nlohmann::json& j);
nlohmann::json formula_to_json(const FormulaJob& f);
FormulaJob formula_from_json(const nlohmann::json& j);
nlohmann::json account_to_json(const Account& a);
Account account_from_json(const nlohmann::json& j);

}  // namespace desk
