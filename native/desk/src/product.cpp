#include "desk/product.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace desk {
namespace {

std::tm local_now() {
  using clock = std::chrono::system_clock;
  auto t = clock::to_time_t(clock::now());
  std::tm tm{};
  localtime_s(&tm, &t);
  return tm;
}

int parse_hms(const std::string& t) {
  int h = 0, m = 0, s = 0;
  if (t.size() >= 5) {
    h = std::atoi(t.substr(0, 2).c_str());
    m = std::atoi(t.substr(3, 2).c_str());
  }
  if (t.size() >= 8) s = std::atoi(t.substr(6, 2).c_str());
  return h * 3600 + m * 60 + s;
}

}  // namespace

std::string new_id() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(0, 15);
  const char* hex = "0123456789abcdef";
  std::string s(10, '0');
  for (char& c : s) c = hex[dist(rng)];
  return s;
}

std::string now_str() {
  auto tm = local_now();
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

namespace {
std::filesystem::path g_holiday_path;
std::unordered_set<std::string> g_holidays;
bool g_holidays_loaded = false;

void ensure_holidays() {
  if (g_holidays_loaded) return;
  g_holidays_loaded = true;
  // A-share public holidays 2025-2026 (approx; override via holidays.json)
  static const char* kBuiltin[] = {
      "20250101", "20250128", "20250129", "20250130", "20250131", "20250201", "20250202", "20250203", "20250204",
      "20250404", "20250405", "20250406", "20250501", "20250502", "20250503", "20250504", "20250505", "20250531",
      "20250601", "20250602", "20251001", "20251002", "20251003", "20251004", "20251005", "20251006", "20251007",
      "20251008", "20260101", "20260102", "20260215", "20260216", "20260217", "20260218", "20260219", "20260220",
      "20260221", "20260222", "20260223", "20260404", "20260405", "20260406", "20260501", "20260502", "20260503",
      "20260504", "20260505", "20260619", "20260620", "20260621", "20260925", "20260926", "20260927", "20261001",
      "20261002", "20261003", "20261004", "20261005", "20261006", "20261007", "20261003"};
  for (auto* d : kBuiltin) g_holidays.insert(d);
  if (!g_holiday_path.empty() && std::filesystem::exists(g_holiday_path)) {
    try {
      std::ifstream in(g_holiday_path);
      nlohmann::json j;
      in >> j;
      if (j.is_array()) {
        g_holidays.clear();
        for (const auto& x : j) {
          if (x.is_string()) g_holidays.insert(x.get<std::string>());
        }
      }
    } catch (...) {
    }
  }
}
}  // namespace

void set_holiday_file(const std::filesystem::path& path) {
  g_holiday_path = path;
  g_holidays_loaded = false;
}

std::string today_ymd() {
  auto tm = local_now();
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return buf;
}

bool is_a_share_trading_day() {
  auto tm = local_now();
  if (tm.tm_wday == 0 || tm.tm_wday == 6) return false;
  ensure_holidays();
  return g_holidays.find(today_ymd()) == g_holidays.end();
}

bool safe_backup_file(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) return true;
  std::error_code ec;
  auto bak = path;
  bak += ".bak";
  std::filesystem::copy_file(path, bak, std::filesystem::copy_options::overwrite_existing, ec);
  return !ec;
}

bool in_session_hms(const std::string& start, const std::string& end) {
  auto tm = local_now();
  int now = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
  int a = parse_hms(start.empty() ? "09:25:00" : start);
  int b = parse_hms(end.empty() ? "15:00:00" : end);
  if (a <= b) return now >= a && now <= b;
  return now >= a || now <= b;
}

int floor_lot(int qty, const std::string& code) {
  int lot = 100;
  auto pos = code.find('.');
  std::string num = pos == std::string::npos ? code : code.substr(0, pos);
  if (num.size() >= 2 && (num.substr(0, 2) == "11" || num.substr(0, 2) == "12")) lot = 10;
  if (qty < lot) return 0;
  return (qty / lot) * lot;
}

nlohmann::json alert_to_json(const AlertRule& a) {
  return nlohmann::json{{"id", a.id},
                        {"name", a.name},
                        {"source", a.source},
                        {"code", a.code},
                        {"sign_name", a.sign_name},
                        {"side", a.side},
                        {"compare", a.compare},
                        {"trigger_price", a.trigger_price},
                        {"trigger_pct", a.trigger_pct},
                        {"ref_price", a.ref_price},
                        {"qty", a.qty},
                        {"qty_unit", a.qty_unit},
                        {"auto_trade", a.auto_trade},
                        {"once", a.once},
                        {"cooldown_sec", a.cooldown_sec},
                        {"enabled", a.enabled},
                        {"status", a.status},
                        {"last_trigger_at", a.last_trigger_at},
                        {"trigger_count", a.trigger_count},
                        {"note", a.note}};
}

AlertRule alert_from_json(const nlohmann::json& j) {
  AlertRule a;
  a.id = j.value("id", "");
  a.name = j.value("name", "");
  a.source = j.value("source", "price");
  a.code = j.value("code", "");
  a.sign_name = j.value("sign_name", "");
  a.side = j.value("side", "buy");
  a.compare = j.value("compare", "gte");
  a.trigger_price = j.value("trigger_price", 0.0);
  a.trigger_pct = j.value("trigger_pct", 0.0);
  a.ref_price = j.value("ref_price", 0.0);
  a.qty = j.value("qty", 100.0);
  a.qty_unit = j.value("qty_unit", "shares");
  a.auto_trade = j.value("auto_trade", true);
  a.once = j.value("once", false);
  a.cooldown_sec = j.value("cooldown_sec", 60);
  a.enabled = j.value("enabled", true);
  a.status = j.value("status", "watching");
  a.last_trigger_at = j.value("last_trigger_at", "");
  a.trigger_count = j.value("trigger_count", 0);
  a.note = j.value("note", "");
  return a;
}

nlohmann::json stop_to_json(const StopRule& s) {
  return nlohmann::json{{"id", s.id},
                        {"code", s.code},
                        {"name", s.name},
                        {"cost_price", s.cost_price},
                        {"stop_pct", s.stop_pct},
                        {"take_pct", s.take_pct},
                        {"stop_price", s.stop_price},
                        {"take_price", s.take_price},
                        {"trail_enabled", s.trail_enabled},
                        {"trail_pct", s.trail_pct},
                        {"high_water", s.high_water},
                        {"qty", s.qty},
                        {"enabled", s.enabled},
                        {"status", s.status},
                        {"last_trigger_at", s.last_trigger_at},
                        {"note", s.note}};
}

StopRule stop_from_json(const nlohmann::json& j) {
  StopRule s;
  s.id = j.value("id", "");
  s.code = j.value("code", "");
  s.name = j.value("name", "");
  s.cost_price = j.value("cost_price", 0.0);
  s.stop_pct = j.value("stop_pct", 5.0);
  s.take_pct = j.value("take_pct", 10.0);
  s.stop_price = j.value("stop_price", 0.0);
  s.take_price = j.value("take_price", 0.0);
  s.trail_enabled = j.value("trail_enabled", false);
  s.trail_pct = j.value("trail_pct", 0.0);
  s.high_water = j.value("high_water", 0.0);
  s.qty = j.value("qty", 100);
  s.enabled = j.value("enabled", true);
  s.status = j.value("status", "watching");
  s.last_trigger_at = j.value("last_trigger_at", "");
  s.note = j.value("note", "");
  return s;
}

nlohmann::json cond_to_json(const CondOrder& c) {
  return nlohmann::json{{"id", c.id},
                        {"code", c.code},
                        {"name", c.name},
                        {"side", c.side},
                        {"kind", c.kind},
                        {"compare", c.compare},
                        {"trigger_price", c.trigger_price},
                        {"trigger_pct", c.trigger_pct},
                        {"trigger_time", c.trigger_time},
                        {"day_anchor", c.day_anchor},
                        {"qty", c.qty},
                        {"price_type", c.price_type},
                        {"limit_price", c.limit_price},
                        {"enabled", c.enabled},
                        {"status", c.status},
                        {"last_trigger_at", c.last_trigger_at},
                        {"note", c.note}};
}

CondOrder cond_from_json(const nlohmann::json& j) {
  CondOrder c;
  c.id = j.value("id", "");
  c.code = j.value("code", "");
  c.name = j.value("name", "");
  c.side = j.value("side", "buy");
  c.kind = j.value("kind", "price");
  c.compare = j.value("compare", "gte");
  c.trigger_price = j.value("trigger_price", 0.0);
  c.trigger_pct = j.value("trigger_pct", 0.0);
  c.trigger_time = j.value("trigger_time", "");
  c.day_anchor = j.value("day_anchor", 0.0);
  c.qty = j.value("qty", 100);
  c.price_type = j.value("price_type", "last");
  c.limit_price = j.value("limit_price", 0.0);
  c.enabled = j.value("enabled", true);
  c.status = j.value("status", "armed");
  c.last_trigger_at = j.value("last_trigger_at", "");
  c.note = j.value("note", "");
  return c;
}

double quote_price_by_type(const grid::Quote& q, const std::string& type) {
  auto level = [](const double levels[5], int idx, double fallback) {
    if (idx >= 0 && idx < 5 && levels[idx] > 0) return levels[idx];
    for (int i = idx; i >= 0; --i)
      if (i < 5 && levels[i] > 0) return levels[i];
    for (int i = idx + 1; i < 5; ++i)
      if (levels[i] > 0) return levels[i];
    return fallback;
  };
  if (type == "market" || type == "last" || type == "newest" || type.empty()) return q.price;
  if (type.rfind("bid", 0) == 0 && type.size() == 4 && type[3] >= '1' && type[3] <= '5')
    return level(q.bids, type[3] - '1', q.bid > 0 ? q.bid : q.price);
  if (type.rfind("ask", 0) == 0 && type.size() == 4 && type[3] >= '1' && type[3] <= '5')
    return level(q.asks, type[3] - '1', q.ask > 0 ? q.ask : q.price);
  if (type == "buy" || type == "bid") return q.bid > 0 ? q.bid : q.price;
  if (type == "sell" || type == "ask") return q.ask > 0 ? q.ask : q.price;
  return q.price;
}

std::string order_price_type_for(const std::string& quote_type) {
  return quote_type == "market" ? "market" : "limit";
}

nlohmann::json watch_to_json(const WatchItem& w) {
  return nlohmann::json{{"id", w.id},       {"code", w.code},       {"name", w.name}, {"tag", w.tag},
                        {"note", w.note},   {"alert_high", w.alert_high}, {"alert_low", w.alert_low}};
}

WatchItem watch_from_json(const nlohmann::json& j) {
  WatchItem w;
  w.id = j.value("id", "");
  w.code = j.value("code", "");
  w.name = j.value("name", "");
  w.tag = j.value("tag", "");
  w.note = j.value("note", "");
  w.alert_high = j.value("alert_high", 0.0);
  w.alert_low = j.value("alert_low", 0.0);
  return w;
}

nlohmann::json risk_to_json(const RiskConfig& r) {
  return nlohmann::json{{"trade_days_only", r.trade_days_only},
                        {"session_only", r.session_only},
                        {"session_start", r.session_start},
                        {"session_end", r.session_end},
                        {"max_orders_per_day", r.max_orders_per_day},
                        {"max_order_amount", r.max_order_amount},
                        {"max_daily_loss", r.max_daily_loss},
                        {"code_cooldown_sec", r.code_cooldown_sec},
                        {"confirm_before_trade", r.confirm_before_trade},
                        {"sound_on_trigger", r.sound_on_trigger},
                        {"notify_log", r.notify_log},
                        {"tdx_sign_file", r.tdx_sign_file},
                        {"sponsor_equity_limit", r.sponsor_equity_limit}};
}

RiskConfig risk_from_json(const nlohmann::json& j) {
  RiskConfig r;
  r.trade_days_only = j.value("trade_days_only", true);
  r.session_only = j.value("session_only", true);
  r.session_start = j.value("session_start", "09:25:00");
  r.session_end = j.value("session_end", "15:00:00");
  r.max_orders_per_day = j.value("max_orders_per_day", 20);
  r.max_order_amount = j.value("max_order_amount", 500000.0);
  r.max_daily_loss = j.value("max_daily_loss", 50000.0);
  r.code_cooldown_sec = j.value("code_cooldown_sec", 60);
  r.confirm_before_trade = j.value("confirm_before_trade", true);
  r.sound_on_trigger = j.value("sound_on_trigger", true);
  r.notify_log = j.value("notify_log", true);
  r.tdx_sign_file = j.value("tdx_sign_file", "");
  r.sponsor_equity_limit = j.value("sponsor_equity_limit", 1000000.0);
  return r;
}

nlohmann::json pending_to_json(const PendingConfirm& p) {
  return nlohmann::json{{"id", p.id},
                        {"code", p.code},
                        {"name", p.name},
                        {"side", p.side},
                        {"qty", p.qty},
                        {"price", p.price},
                        {"price_type", p.price_type},
                        {"reason", p.reason},
                        {"created_at", p.created_at}};
}

bool load_json_array(const std::filesystem::path& path, nlohmann::json& out, std::string& err) {
  if (!std::filesystem::exists(path)) {
    out = nlohmann::json::array();
    return true;
  }
  std::ifstream in(path);
  if (!in) {
    err = "cannot open " + path.u8string();
    return false;
  }
  try {
    in >> out;
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
  if (!out.is_array()) {
    err = "root must be array";
    return false;
  }
  return true;
}

bool save_json_array(const std::filesystem::path& path, const nlohmann::json& arr, std::string& err) {
  safe_backup_file(path);
  auto tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary);
    if (!out) {
      err = "cannot write";
      return false;
    }
    out << arr.dump(2);
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    err = ec.message();
    return false;
  }
  return true;
}

bool load_json_object(const std::filesystem::path& path, nlohmann::json& out, std::string& err) {
  if (!std::filesystem::exists(path)) {
    out = nlohmann::json::object();
    return true;
  }
  std::ifstream in(path);
  if (!in) {
    err = "cannot open";
    return false;
  }
  try {
    in >> out;
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
  if (!out.is_object()) {
    err = "root must be object";
    return false;
  }
  return true;
}

bool save_json_object(const std::filesystem::path& path, const nlohmann::json& obj, std::string& err) {
  return save_json_array(path, obj, err);  // same write pattern works for object dump
}

nlohmann::json t0_to_json(const T0Rule& r) {
  return nlohmann::json{{"id", r.id},
                        {"code", r.code},
                        {"name", r.name},
                        {"buy_down_pct", r.buy_down_pct},
                        {"sell_up_pct", r.sell_up_pct},
                        {"buy_qty", r.buy_qty},
                        {"sell_qty", r.sell_qty},
                        {"qty", r.buy_qty},
                        {"base_price", r.base_price},
                        {"day_anchor", r.day_anchor},
                        {"order_type", r.order_type},
                        {"long_today", r.long_today},
                        {"enabled", r.enabled},
                        {"session_only", r.session_only},
                        {"max_rounds", r.max_rounds},
                        {"rounds_today", r.rounds_today},
                        {"day_key", r.day_key},
                        {"cooldown_sec", r.cooldown_sec},
                        {"last_action_ms", r.last_action_ms},
                        {"status", r.status},
                        {"last_trigger_at", r.last_trigger_at},
                        {"note", r.note}};
}
T0Rule t0_from_json(const nlohmann::json& j) {
  T0Rule r;
  r.id = j.value("id", "");
  r.code = j.value("code", "");
  r.name = j.value("name", "");
  r.buy_down_pct = j.value("buy_down_pct", 1.0);
  r.sell_up_pct = j.value("sell_up_pct", 1.0);
  r.buy_qty = j.value("buy_qty", j.value("qty", 100));
  r.sell_qty = j.value("sell_qty", 0);
  r.qty = r.buy_qty;
  r.base_price = j.value("base_price", j.value("day_anchor", 0.0));
  r.day_anchor = j.value("day_anchor", r.base_price);
  r.order_type = j.value("order_type", "market");
  if (r.order_type.empty()) r.order_type = "market";
  r.long_today = j.value("long_today", false);
  r.enabled = j.value("enabled", true);
  r.session_only = j.value("session_only", true);
  r.max_rounds = j.value("max_rounds", 0);
  r.rounds_today = j.value("rounds_today", 0);
  r.day_key = j.value("day_key", "");
  r.cooldown_sec = j.value("cooldown_sec", 3);
  r.last_action_ms = j.value("last_action_ms", (int64_t)0);
  r.status = j.value("status", "watching");
  r.last_trigger_at = j.value("last_trigger_at", "");
  r.note = j.value("note", "");
  return r;
}

nlohmann::json basket_to_json(const Basket& b) {
  nlohmann::json legs = nlohmann::json::array();
  for (const auto& l : b.legs) {
    legs.push_back({{"code", l.code}, {"side", l.side}, {"qty", l.qty}, {"weight", l.weight}});
  }
  return nlohmann::json{{"id", b.id}, {"name", b.name}, {"capital", b.capital}, {"legs", legs}, {"note", b.note}};
}
Basket basket_from_json(const nlohmann::json& j) {
  Basket b;
  b.id = j.value("id", "");
  b.name = j.value("name", "");
  b.capital = j.value("capital", 0.0);
  b.note = j.value("note", "");
  if (j.contains("legs") && j["legs"].is_array()) {
    for (const auto& x : j["legs"]) {
      BasketLeg l;
      l.code = x.value("code", "");
      l.side = x.value("side", "buy");
      l.qty = x.value("qty", 100);
      l.weight = x.value("weight", 0.0);
      b.legs.push_back(l);
    }
  }
  return b;
}

nlohmann::json board_to_json(const BoardRule& b) {
  return nlohmann::json{{"id", b.id},
                        {"code", b.code},
                        {"name", b.name},
                        {"seal_pct", b.seal_pct},
                        {"qty", b.qty},
                        {"enabled", b.enabled},
                        {"status", b.status},
                        {"last_trigger_at", b.last_trigger_at},
                        {"day_fired", b.day_fired}};
}
BoardRule board_from_json(const nlohmann::json& j) {
  BoardRule b;
  b.id = j.value("id", "");
  b.code = j.value("code", "");
  b.name = j.value("name", "");
  b.seal_pct = j.value("seal_pct", 9.5);
  b.qty = j.value("qty", 100);
  b.enabled = j.value("enabled", true);
  b.status = j.value("status", "watching");
  b.last_trigger_at = j.value("last_trigger_at", "");
  b.day_fired = j.value("day_fired", "");
  return b;
}

nlohmann::json formula_to_json(const FormulaJob& f) {
  return nlohmann::json{{"id", f.id},
                        {"name", f.name},
                        {"side", f.side},
                        {"qty", f.qty},
                        {"qty_unit", f.qty_unit},
                        {"enabled", f.enabled},
                        {"status", f.status},
                        {"last_trigger_at", f.last_trigger_at},
                        {"trigger_count", f.trigger_count}};
}
FormulaJob formula_from_json(const nlohmann::json& j) {
  FormulaJob f;
  f.id = j.value("id", "");
  f.name = j.value("name", "");
  f.side = j.value("side", "buy");
  f.qty = j.value("qty", 100.0);
  f.qty_unit = j.value("qty_unit", "shares");
  f.enabled = j.value("enabled", true);
  f.status = j.value("status", "watching");
  f.last_trigger_at = j.value("last_trigger_at", "");
  f.trigger_count = j.value("trigger_count", 0);
  return f;
}

nlohmann::json account_to_json(const Account& a) {
  return nlohmann::json{{"id", a.id},
                        {"label", a.label},
                        {"broker_channel", a.broker_channel},
                        {"capital", a.capital},
                        {"cash", a.cash},
                        {"equity", a.equity},
                        {"enabled", a.enabled},
                        {"note", a.note}};
}
Account account_from_json(const nlohmann::json& j) {
  Account a;
  a.id = j.value("id", "");
  a.label = j.value("label", "");
  a.broker_channel = j.value("broker_channel", "paper");
  a.capital = j.value("capital", 1000000.0);
  a.cash = j.value("cash", a.capital);
  a.equity = j.value("equity", a.cash);
  a.enabled = j.value("enabled", true);
  a.note = j.value("note", "");
  return a;
}

}  // namespace desk
