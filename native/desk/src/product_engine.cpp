#include "desk/product_engine.hpp"

#include "desk/settings.hpp"
#include "grid/http_win.hpp"
#include "grid/order_file.hpp"
#include "grid/quote_factory.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace desk {
namespace {

std::string upper(std::string s) {
  for (auto& c : s) c = (char)toupper((unsigned char)c);
  return s;
}

void beep_if(bool on) {
  if (!on) return;
#ifdef _WIN32
  MessageBeep(MB_ICONEXCLAMATION);
#endif
}

bool price_hit(const std::string& compare, double px, double trigger) {
  if (trigger <= 0 || px <= 0) return false;
  if (compare == "lte") return px <= trigger;
  return px >= trigger;
}

std::string now_hms() {
  auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

int parse_hms_local(const std::string& s) {
  int h = 0, m = 0, sec = 0;
#ifdef _WIN32
  if (sscanf_s(s.c_str(), "%d:%d:%d", &h, &m, &sec) >= 2) return h * 3600 + m * 60 + sec;
#else
  if (std::sscanf(s.c_str(), "%d:%d:%d", &h, &m, &sec) >= 2) return h * 3600 + m * 60 + sec;
#endif
  return -1;
}

bool time_reached(const std::string& trigger_time) {
  if (trigger_time.empty()) return false;
  int want = parse_hms_local(trigger_time);
  int now = parse_hms_local(now_hms());
  if (want < 0 || now < 0) return false;
  return now >= want;
}

std::string extract_code_from_line(const std::string& line) {
  for (size_t i = 0; i + 6 <= line.size(); ++i) {
    bool dig = true;
    for (int k = 0; k < 6; ++k) {
      if (!std::isdigit((unsigned char)line[i + k])) {
        dig = false;
        break;
      }
    }
    if (!dig) continue;
    if (i > 0 && std::isdigit((unsigned char)line[i - 1])) continue;
    if (i + 6 < line.size() && std::isdigit((unsigned char)line[i + 6])) continue;
    return grid::normalize_code(line.substr(i, 6));
  }
  return {};
}

double load_accounts_equity(const std::filesystem::path& data_dir) {
  nlohmann::json arr;
  std::string err;
  if (!load_json_array(data_dir / "accounts.json", arr, err) || !arr.is_array()) return 0;
  double eq = 0;
  for (const auto& j : arr) {
    if (!j.value("enabled", true)) continue;
    eq += j.value("equity", j.value("cash", 0.0));
  }
  return eq;
}

}  // namespace

void ProductEngine::init(const std::filesystem::path& data_dir, const std::filesystem::path& orders_dir) {
  std::lock_guard<std::mutex> lock(mu_);
  data_dir_ = data_dir;
  orders_dir_ = orders_dir;
  alerts_path_ = data_dir_ / "alerts.json";
  stops_path_ = data_dir_ / "stoploss.json";
  conds_path_ = data_dir_ / "conditions.json";
  watch_path_ = data_dir_ / "watchlist.json";
  risk_path_ = data_dir_ / "risk.json";
  risk_state_path_ = data_dir_ / "risk_state.json";
  events_path_ = data_dir_ / "events.json";
  confirms_path_ = data_dir_ / "pending_confirms.json";
  disclaimer_path_ = data_dir_ / "disclaimer.json";
  reload_locked();
}

void ProductEngine::reload_locked() {
  std::string err;
  nlohmann::json arr;
  alerts_.clear();
  if (load_json_array(alerts_path_, arr, err)) {
    for (const auto& j : arr) alerts_.push_back(alert_from_json(j));
  }
  stops_.clear();
  if (load_json_array(stops_path_, arr, err)) {
    for (const auto& j : arr) stops_.push_back(stop_from_json(j));
  }
  conds_.clear();
  if (load_json_array(conds_path_, arr, err)) {
    for (const auto& j : arr) conds_.push_back(cond_from_json(j));
  }
  watch_.clear();
  if (load_json_array(watch_path_, arr, err)) {
    for (const auto& j : arr) watch_.push_back(watch_from_json(j));
  }
  confirms_.clear();
  if (load_json_array(confirms_path_, arr, err)) {
    for (const auto& j : arr) {
      PendingConfirm p;
      p.id = j.value("id", "");
      p.code = j.value("code", "");
      p.name = j.value("name", "");
      p.side = j.value("side", "buy");
      p.qty = j.value("qty", 0);
      p.price = j.value("price", 0.0);
      p.price_type = j.value("price_type", "limit");
      p.reason = j.value("reason", "");
      p.created_at = j.value("created_at", "");
      if (!p.id.empty()) confirms_.push_back(p);
    }
  }
  nlohmann::json obj;
  if (load_json_object(risk_path_, obj, err) && !obj.empty()) risk_ = risk_from_json(obj);
  if (load_json_object(risk_state_path_, obj, err) && !obj.empty()) {
    risk_state_.day = obj.value("day", "");
    risk_state_.orders_today = obj.value("orders_today", 0);
    risk_state_.day_start_equity = obj.value("day_start_equity", 0.0);
    risk_state_.paused_loss = obj.value("paused_loss", false);
    risk_state_.sponsor_reminded_day = obj.value("sponsor_reminded_day", "");
  }
  if (load_json_object(disclaimer_path_, obj, err) && !obj.empty()) {
    disclaimer_accepted_ = obj.value("accepted", false);
  } else {
    disclaimer_accepted_ = false;
  }
  if (risk_state_.day != today_ymd()) {
    risk_state_.day = today_ymd();
    risk_state_.orders_today = 0;
    risk_state_.paused_loss = false;
    risk_state_.day_start_equity = 0;
    persist_risk_locked();
  }
}

void ProductEngine::persist_alerts_locked() {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& a : alerts_) arr.push_back(alert_to_json(a));
  std::string err;
  save_json_array(alerts_path_, arr, err);
}
void ProductEngine::persist_stops_locked() {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& s : stops_) arr.push_back(stop_to_json(s));
  std::string err;
  save_json_array(stops_path_, arr, err);
}
void ProductEngine::persist_conds_locked() {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& c : conds_) arr.push_back(cond_to_json(c));
  std::string err;
  save_json_array(conds_path_, arr, err);
}
void ProductEngine::persist_watch_locked() {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& w : watch_) arr.push_back(watch_to_json(w));
  std::string err;
  save_json_array(watch_path_, arr, err);
}
void ProductEngine::persist_risk_locked() {
  std::string err;
  save_json_object(risk_path_, risk_to_json(risk_), err);
  save_json_object(risk_state_path_,
                   nlohmann::json{{"day", risk_state_.day},
                                  {"orders_today", risk_state_.orders_today},
                                  {"day_start_equity", risk_state_.day_start_equity},
                                  {"paused_loss", risk_state_.paused_loss},
                                  {"sponsor_reminded_day", risk_state_.sponsor_reminded_day}},
                   err);
}

void ProductEngine::persist_confirms_locked() {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& p : confirms_) arr.push_back(pending_to_json(p));
  std::string err;
  save_json_array(confirms_path_, arr, err);
}

void ProductEngine::append_event(const std::string& kind, const std::string& msg, const nlohmann::json& extra) {
  if (!risk_.notify_log) return;
  nlohmann::json arr;
  std::string err;
  load_json_array(events_path_, arr, err);
  nlohmann::json ev = {{"ts", now_str()}, {"kind", kind}, {"msg", msg}};
  if (!extra.empty()) ev["extra"] = extra;
  arr.push_back(ev);
  while (arr.size() > 500) arr.erase(arr.begin());
  save_json_array(events_path_, arr, err);
}

void ProductEngine::refresh_day_equity_locked() {
  double eq = load_accounts_equity(data_dir_);
  if (eq <= 0) return;
  if (risk_state_.day_start_equity <= 0) {
    risk_state_.day_start_equity = eq;
    persist_risk_locked();
  }
  if (risk_.max_daily_loss > 0 && risk_state_.day_start_equity > 0) {
    double loss = risk_state_.day_start_equity - eq;
    if (loss >= risk_.max_daily_loss && !risk_state_.paused_loss) {
      risk_state_.paused_loss = true;
      persist_risk_locked();
      append_event("risk_pause", "日亏损达上限，已暂停自动交易",
                   {{"loss", loss}, {"limit", risk_.max_daily_loss}});
      sound_armed_ = true;
    }
  }
  if (risk_.sponsor_equity_limit > 0 && eq >= risk_.sponsor_equity_limit &&
      risk_state_.sponsor_reminded_day != today_ymd()) {
    risk_state_.sponsor_reminded_day = today_ymd();
    persist_risk_locked();
    append_event("sponsor", "账户权益超过赞助门槛，请支持年费赞助（软提醒，不锁定）",
                 {{"equity", eq}, {"limit", risk_.sponsor_equity_limit}});
    sound_armed_ = true;
  }
}

bool ProductEngine::risk_allows_order(const std::string& code, int qty, double price, std::string& why) {
  if (risk_state_.day != today_ymd()) {
    risk_state_.day = today_ymd();
    risk_state_.orders_today = 0;
    risk_state_.paused_loss = false;
    risk_state_.day_start_equity = 0;
  }
  if (risk_state_.paused_loss) {
    why = "paused: daily loss limit";
    return false;
  }
  if (risk_.trade_days_only && !is_a_share_trading_day()) {
    why = "non-trading day";
    return false;
  }
  if (risk_.session_only && !in_session_hms(risk_.session_start, risk_.session_end)) {
    why = "outside session";
    return false;
  }
  if (risk_.max_orders_per_day > 0 && risk_state_.orders_today >= risk_.max_orders_per_day) {
    why = "max orders/day reached";
    return false;
  }
  if (risk_.max_order_amount > 0 && price > 0 && qty > 0) {
    double amt = price * qty;
    if (amt > risk_.max_order_amount) {
      why = "max order amount exceeded";
      return false;
    }
  }
  if (risk_.code_cooldown_sec > 0 && !code.empty()) {
    auto key = upper(code);
    auto it = code_last_order_ms_.find(key);
    if (it != code_last_order_ms_.end()) {
      int64_t elapsed = now_ms() - it->second;
      if (elapsed < risk_.code_cooldown_sec * 1000LL) {
        why = "code cooldown";
        return false;
      }
    }
  }
  return true;
}

void ProductEngine::bump_order_count(const std::string& code) {
  if (risk_state_.day != today_ymd()) {
    risk_state_.day = today_ymd();
    risk_state_.orders_today = 0;
  }
  risk_state_.orders_today += 1;
  if (!code.empty()) code_last_order_ms_[upper(code)] = now_ms();
  persist_risk_locked();
}

bool ProductEngine::emit_trade(const std::string& code, const std::string& side, int qty, double price,
                               const std::string& reason, const std::string& name, std::string& err,
                               const std::string& price_type, bool skip_confirm) {
  std::string why;
  if (!risk_allows_order(code, qty, price, why)) {
    err = why;
    return false;
  }
  const std::string ptype = price_type.empty() ? "limit" : price_type;
  const bool bypass_confirm =
      skip_confirm || reason == "flash" || reason.rfind("confirm:", 0) == 0 || reason.rfind("manual", 0) == 0;
  const bool force_confirm = reason.rfind("confirm_force:", 0) == 0;
  if ((risk_.confirm_before_trade || force_confirm) && !bypass_confirm) {
    const std::string ucode = upper(code);
    for (const auto& existing : confirms_) {
      if (existing.code == ucode && existing.side == side && existing.reason == reason) {
        err = "need_confirm";
        return false;
      }
    }
    PendingConfirm p;
    p.id = new_id();
    p.code = ucode;
    p.name = name;
    p.side = side;
    p.qty = qty;
    p.price = price;
    p.price_type = ptype;
    p.reason = reason;
    p.created_at = now_str();
    confirms_.push_back(p);
    while (confirms_.size() > 100) confirms_.erase(confirms_.begin());
    persist_confirms_locked();
    append_event("confirm_pending", reason + " " + side + " " + code + " x" + std::to_string(qty),
                 {{"id", p.id}, {"price", price}});
    sound_armed_ = true;
    err = "need_confirm";
    return false;
  }
  auto settings = SettingsStore::instance().load();
  bool write = settings.broker_channel != "paper";
  if (!write) {
    bump_order_count(code);
    append_event("paper", reason + " " + side + " " + code + " x" + std::to_string(qty),
                 {{"price", price}, {"price_type", ptype}, {"name", name}});
    return true;
  }
  grid::OrderIntent intent;
  intent.code = upper(code);
  intent.side = side;
  intent.qty = qty;
  intent.price = price;
  intent.price_type = ptype;
  intent.name = name;
  intent.reason = reason;
  if (!grid::write_order_json(orders_dir_, intent, err)) return false;
  bump_order_count(code);
  append_event("order", reason + " " + side + " " + code + " x" + std::to_string(qty),
               {{"price", price}, {"price_type", ptype}, {"client_order_id", intent.client_order_id}});
  return true;
}

nlohmann::json ProductEngine::list_alerts() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& a : alerts_) arr.push_back(alert_to_json(a));
  return arr;
}

bool ProductEngine::upsert_alert(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  AlertRule a = alert_from_json(body);
  if (a.id.empty()) a.id = new_id();
  if (a.name.empty()) a.name = a.code.empty() ? a.sign_name : a.code;
  bool found = false;
  for (auto& x : alerts_) {
    if (x.id == a.id) {
      a.trigger_count = x.trigger_count;
      a.last_trigger_at = x.last_trigger_at;
      a.last_trigger_ms = x.last_trigger_ms;
      x = a;
      found = true;
      break;
    }
  }
  if (!found) alerts_.push_back(a);
  persist_alerts_locked();
  out = alert_to_json(a);
  return true;
}

bool ProductEngine::delete_alert(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto n = alerts_.size();
  alerts_.erase(std::remove_if(alerts_.begin(), alerts_.end(), [&](const AlertRule& a) { return a.id == id; }),
                alerts_.end());
  if (alerts_.size() == n) {
    err = "not found";
    return false;
  }
  persist_alerts_locked();
  return true;
}

bool ProductEngine::toggle_alert(const std::string& id, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  for (auto& a : alerts_) {
    if (a.id != id) continue;
    a.enabled = !a.enabled;
    a.status = a.enabled ? "watching" : "paused";
    persist_alerts_locked();
    out = alert_to_json(a);
    return true;
  }
  err = "not found";
  return false;
}

nlohmann::json ProductEngine::list_stops() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& s : stops_) arr.push_back(stop_to_json(s));
  return arr;
}
bool ProductEngine::upsert_stop(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  StopRule s = stop_from_json(body);
  if (s.id.empty()) s.id = new_id();
  if (!s.code.empty()) s.code = grid::normalize_code(s.code);
  bool found = false;
  for (auto& x : stops_) {
    if (x.id == s.id) {
      if (!body.contains("high_water")) s.high_water = x.high_water;
      if (s.last_trigger_at.empty()) s.last_trigger_at = x.last_trigger_at;
      x = s;
      found = true;
      break;
    }
  }
  if (!found) stops_.push_back(s);
  persist_stops_locked();
  out = stop_to_json(s);
  return true;
}
bool ProductEngine::delete_stop(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto n = stops_.size();
  stops_.erase(std::remove_if(stops_.begin(), stops_.end(), [&](const StopRule& s) { return s.id == id; }),
               stops_.end());
  if (stops_.size() == n) {
    err = "not found";
    return false;
  }
  persist_stops_locked();
  return true;
}
bool ProductEngine::toggle_stop(const std::string& id, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  for (auto& s : stops_) {
    if (s.id != id) continue;
    s.enabled = !s.enabled;
    s.status = s.enabled ? "watching" : "paused";
    persist_stops_locked();
    out = stop_to_json(s);
    return true;
  }
  err = "not found";
  return false;
}

nlohmann::json ProductEngine::list_conds() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& c : conds_) arr.push_back(cond_to_json(c));
  return arr;
}
bool ProductEngine::upsert_cond(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  CondOrder c = cond_from_json(body);
  if (c.id.empty()) c.id = new_id();
  if (!c.code.empty()) c.code = grid::normalize_code(c.code);
  bool found = false;
  for (auto& x : conds_) {
    if (x.id == c.id) {
      if (!body.contains("day_anchor")) c.day_anchor = x.day_anchor;
      if (c.last_trigger_at.empty()) c.last_trigger_at = x.last_trigger_at;
      x = c;
      found = true;
      break;
    }
  }
  if (!found) conds_.push_back(c);
  persist_conds_locked();
  out = cond_to_json(c);
  return true;
}
bool ProductEngine::delete_cond(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto n = conds_.size();
  conds_.erase(std::remove_if(conds_.begin(), conds_.end(), [&](const CondOrder& c) { return c.id == id; }),
               conds_.end());
  if (conds_.size() == n) {
    err = "not found";
    return false;
  }
  persist_conds_locked();
  return true;
}
bool ProductEngine::toggle_cond(const std::string& id, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  for (auto& c : conds_) {
    if (c.id != id) continue;
    if (c.status == "filled") {
      err = "already filled";
      return false;
    }
    c.enabled = !c.enabled;
    c.status = c.enabled ? "armed" : "cancelled";
    persist_conds_locked();
    out = cond_to_json(c);
    return true;
  }
  err = "not found";
  return false;
}

nlohmann::json ProductEngine::list_watch() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& w : watch_) arr.push_back(watch_to_json(w));
  return arr;
}
bool ProductEngine::upsert_watch(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  WatchItem w = watch_from_json(body);
  if (w.id.empty()) w.id = new_id();
  w.code = upper(w.code);
  bool found = false;
  for (auto& x : watch_) {
    if (x.id == w.id || upper(x.code) == w.code) {
      w.id = x.id;
      x = w;
      found = true;
      break;
    }
  }
  if (!found) watch_.push_back(w);
  persist_watch_locked();
  out = watch_to_json(w);
  return true;
}
bool ProductEngine::delete_watch(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto n = watch_.size();
  watch_.erase(std::remove_if(watch_.begin(), watch_.end(), [&](const WatchItem& w) { return w.id == id; }),
               watch_.end());
  if (watch_.size() == n) {
    err = "not found";
    return false;
  }
  persist_watch_locked();
  return true;
}

nlohmann::json ProductEngine::risk_json() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  refresh_day_equity_locked();
  auto j = risk_to_json(risk_);
  j["orders_today"] = risk_state_.orders_today;
  j["day"] = risk_state_.day;
  j["day_start_equity"] = risk_state_.day_start_equity;
  j["paused_loss"] = risk_state_.paused_loss;
  j["pending_confirms"] = (int)confirms_.size();
  j["disclaimer_accepted"] = disclaimer_accepted_;
  j["account_equity"] = load_accounts_equity(data_dir_);
  j["sponsor_due"] = j["account_equity"].get<double>() >= risk_.sponsor_equity_limit && risk_.sponsor_equity_limit > 0;
  j["is_trading_day"] = is_a_share_trading_day();
  j["in_session"] = in_session_hms(risk_.session_start, risk_.session_end);
  return j;
}
bool ProductEngine::save_risk(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  risk_ = risk_from_json(body);
  // hard limits always stay positive (cannot disable by zeroing via UI tricks for key caps)
  if (risk_.max_orders_per_day <= 0) risk_.max_orders_per_day = 20;
  if (risk_.max_order_amount <= 0) risk_.max_order_amount = 500000;
  if (risk_.max_daily_loss <= 0) risk_.max_daily_loss = 50000;
  if (risk_.code_cooldown_sec < 0) risk_.code_cooldown_sec = 60;
  persist_risk_locked();
  out = risk_to_json(risk_);
  out["orders_today"] = risk_state_.orders_today;
  out["day"] = risk_state_.day;
  out["day_start_equity"] = risk_state_.day_start_equity;
  out["paused_loss"] = risk_state_.paused_loss;
  out["pending_confirms"] = (int)confirms_.size();
  out["disclaimer_accepted"] = disclaimer_accepted_;
  out["account_equity"] = load_accounts_equity(data_dir_);
  out["sponsor_due"] =
      out["account_equity"].get<double>() >= risk_.sponsor_equity_limit && risk_.sponsor_equity_limit > 0;
  out["is_trading_day"] = is_a_share_trading_day();
  out["in_session"] = in_session_hms(risk_.session_start, risk_.session_end);
  return true;
}

nlohmann::json ProductEngine::orders_json(int limit) {
  namespace fs = std::filesystem;
  nlohmann::json arr = nlohmann::json::array();
  for (const char* folder : {"outbox", "sent", "ack", "done", "reject"}) {
    std::error_code ec;
    auto dir = orders_dir_ / folder;
    if (!fs::exists(dir, ec)) continue;
    for (auto& ent : fs::directory_iterator(dir, ec)) {
      if (ent.path().extension() != ".json") continue;
      try {
        std::ifstream in(ent.path());
        nlohmann::json j;
        in >> j;
        j["folder"] = folder;
        j["file"] = ent.path().filename().u8string();
        arr.push_back(j);
      } catch (...) {
      }
    }
  }
  std::sort(arr.begin(), arr.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
    return a.value("created_at", "") > b.value("created_at", "");
  });
  if ((int)arr.size() > limit) {
    nlohmann::json trimmed = nlohmann::json::array();
    for (int i = 0; i < limit; ++i) trimmed.push_back(arr[i]);
    arr = std::move(trimmed);
  }
  return arr;
}

nlohmann::json ProductEngine::events_json(int limit) {
  std::lock_guard<std::mutex> lock(mu_);
  nlohmann::json arr;
  std::string err;
  load_json_array(events_path_, arr, err);
  if (!arr.is_array()) return nlohmann::json::array();
  nlohmann::json out = nlohmann::json::array();
  int from = (int)arr.size() > limit ? (int)arr.size() - limit : 0;
  for (int i = (int)arr.size() - 1; i >= from; --i) out.push_back(arr[i]);
  return out;
}

nlohmann::json ProductEngine::dashboard_json() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  refresh_day_equity_locked();
  int alert_on = 0, stop_on = 0, cond_on = 0;
  for (const auto& a : alerts_)
    if (a.enabled) ++alert_on;
  for (const auto& s : stops_)
    if (s.enabled) ++stop_on;
  for (const auto& c : conds_)
    if (c.enabled && c.status == "armed") ++cond_on;
  double eq = load_accounts_equity(data_dir_);
  return nlohmann::json{{"alerts", alerts_.size()},
                        {"alerts_on", alert_on},
                        {"stops", stops_.size()},
                        {"stops_on", stop_on},
                        {"conditions", conds_.size()},
                        {"conditions_armed", cond_on},
                        {"watchlist", watch_.size()},
                        {"orders_today", risk_state_.orders_today},
                        {"pending_confirms", (int)confirms_.size()},
                        {"paused_loss", risk_state_.paused_loss},
                        {"disclaimer_accepted", disclaimer_accepted_},
                        {"account_equity", eq},
                        {"sponsor_due", eq >= risk_.sponsor_equity_limit && risk_.sponsor_equity_limit > 0},
                        {"is_trading_day", is_a_share_trading_day()},
                        {"in_session", in_session_hms(risk_.session_start, risk_.session_end)}};
}

nlohmann::json ProductEngine::disclaimer_json() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  return nlohmann::json{{"accepted", disclaimer_accepted_},
                        {"version", "1.0"},
                        {"title", "使用协议与风险揭示"},
                        {"text",
                         "本工具仅供本地辅助下单，不构成投资建议，不保证交易结果。\n"
                         "因软件版本不匹配、网络异常、系统故障、券商界面变更等导致的交易损失，开发者不承担责任。\n"
                         "用户须自行管理账户密码与资金安全，了解证券交易风险后再使用。\n"
                         "程序不保存券商密码、不上传账户数据。超过赞助门槛时仅软提醒，不强制锁定。"}};
}

bool ProductEngine::accept_disclaimer(std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  disclaimer_accepted_ = true;
  nlohmann::json obj = {{"accepted", true}, {"accepted_at", now_str()}, {"version", "1.0"}};
  if (!save_json_object(disclaimer_path_, obj, err)) return false;
  append_event("disclaimer", "用户已同意使用协议");
  return true;
}

nlohmann::json ProductEngine::list_confirms() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& p : confirms_) arr.push_back(pending_to_json(p));
  return arr;
}

bool ProductEngine::decide_confirm(const std::string& id, bool accept, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto it = std::find_if(confirms_.begin(), confirms_.end(), [&](const PendingConfirm& p) { return p.id == id; });
  if (it == confirms_.end()) {
    err = "confirm not found";
    return false;
  }
  PendingConfirm p = *it;
  confirms_.erase(it);
  persist_confirms_locked();
  if (!accept) {
    append_event("confirm_reject", p.reason + " " + p.side + " " + p.code, {{"id", p.id}});
    out = {{"ok", true}, {"accepted", false}, {"id", p.id}};
    return true;
  }
  if (!emit_trade(p.code, p.side, p.qty, p.price, "confirm:" + p.reason, p.name, err, p.price_type, true)) {
    return false;
  }
  out = {{"ok", true},
         {"accepted", true},
         {"id", p.id},
         {"code", p.code},
         {"side", p.side},
         {"qty", p.qty},
         {"price", p.price}};
  return true;
}

nlohmann::json ProductEngine::version_json() {
  return nlohmann::json{{"version", "1.0.0"},
                        {"name", "跟单台"},
                        {"channel", "mvp"},
                        {"update_url", ""},
                        {"notes", "本地 MVP：条件单/止盈止损/预警/风控/半自动确认"}};
}

bool ProductEngine::place_order(const std::string& code, const std::string& side, int qty, double price,
                                const std::string& reason, const std::string& name, std::string& err,
                                const std::string& price_type) {
  std::lock_guard<std::mutex> lock(mu_);
  const bool skip = reason == "flash" || reason.rfind("confirm:", 0) == 0 || reason.rfind("manual", 0) == 0 ||
                    reason.rfind("basket", 0) == 0;
  return emit_trade(code, side, qty, price, reason, name, err, price_type, skip);
}

void ProductEngine::note_event(const std::string& kind, const std::string& msg) {
  std::lock_guard<std::mutex> lock(mu_);
  append_event(kind, msg);
}

bool ProductEngine::flash_order(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::string code = upper(body.value("code", ""));
  std::string side = body.value("side", "buy");
  int qty = body.value("qty", 0);
  double price = body.value("price", 0.0);
  std::string name = body.value("name", "");
  std::string px_type = body.value("price_type", side == "sell" ? "bid1" : "ask1");
  if (code.empty() || qty <= 0) {
    err = "code/qty required";
    return false;
  }
  qty = floor_lot(qty, code);
  if (qty <= 0) {
    err = "qty below lot";
    return false;
  }
  if (price <= 0) {
    std::unordered_map<std::string, grid::Quote> quotes;
    std::string qerr;
    auto src = SettingsStore::instance().load().quote_source;
    if (grid::fetch_quotes(src.empty() ? "tencent" : src, {code}, quotes, qerr)) {
      auto it = quotes.find(code);
      if (it != quotes.end()) price = quote_price_by_type(it->second, px_type);
    }
  }
  if (price <= 0) {
    err = "no price";
    return false;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (!emit_trade(code, side, qty, price, "flash", name, err, order_price_type_for(px_type), true)) return false;
  out = {{"ok", true},
         {"code", code},
         {"side", side},
         {"qty", qty},
         {"price", price},
         {"price_type", order_price_type_for(px_type)}};
  return true;
}

void ProductEngine::scan_file_signals(std::string& log_line) {
  if (risk_.tdx_sign_file.empty()) return;
  namespace fs = std::filesystem;
  fs::path root = fs::u8path(risk_.tdx_sign_file);
  if (!fs::exists(root)) return;

  std::vector<fs::path> files;
  std::error_code ec;
  if (fs::is_directory(root, ec)) {
    for (auto& ent : fs::directory_iterator(root, ec)) {
      if (!ent.is_regular_file(ec)) continue;
      auto ext = ent.path().extension().string();
      for (auto& c : ext) c = (char)tolower((unsigned char)c);
      if (ext != ".txt" && ext != ".csv" && ext != ".sign" && ext != ".dat" && ext != "") continue;
      files.push_back(ent.path());
    }
    std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
      auto ta = fs::last_write_time(a, ec);
      auto tb = fs::last_write_time(b, ec);
      return ta > tb;
    });
    if (files.size() > 8) files.resize(8);
  } else {
    files.push_back(root);
  }
  if (files.empty()) return;

  std::string content;
  for (const auto& fp : files) {
    std::ifstream in(fp);
    if (!in) continue;
    std::stringstream ss;
    ss << in.rdbuf();
    content += ss.str();
    content.push_back('\n');
  }
  if (content.empty()) return;

  std::string fp = std::to_string(content.size()) + ":" +
                   (content.size() > 120 ? content.substr(content.size() - 120) : content);
  if (fp == last_sign_fingerprint_) return;
  last_sign_fingerprint_ = fp;

  bool dirty = false;
  std::stringstream ls(content);
  std::string line;
  while (std::getline(ls, line)) {
    if (line.empty()) continue;
    // strip UTF-8 BOM / CR
    if (!line.empty() && (unsigned char)line.back() == '\r') line.pop_back();
    for (auto& a : alerts_) {
      if (!a.enabled || a.source != "file") continue;
      if (a.sign_name.empty()) continue;
      if (line.find(a.sign_name) == std::string::npos) continue;

      std::string code = a.code.empty() ? "" : grid::normalize_code(a.code);
      std::string extracted = extract_code_from_line(line);
      if (!extracted.empty()) code = extracted;
      if (code.empty()) continue;

      int64_t t = now_ms();
      if (a.cooldown_sec > 0 && a.last_trigger_ms > 0 && t - a.last_trigger_ms < a.cooldown_sec * 1000LL) continue;

      a.last_trigger_at = now_str();
      a.last_trigger_ms = t;
      a.trigger_count += 1;
      a.status = "triggered";
      dirty = true;
      sound_armed_ = true;
      append_event("alert_file", a.name + " " + code, {{"line", line}, {"sign", a.sign_name}});

      if (a.auto_trade && a.side != "notify") {
        int qty = (int)std::llround(a.qty);
        double px = 0;
        std::unordered_map<std::string, grid::Quote> quotes;
        std::string qerr;
        auto src = SettingsStore::instance().load().quote_source;
        grid::fetch_quotes(src.empty() ? "tencent" : src, {code}, quotes, qerr);
        auto it = quotes.find(upper(code));
        std::string qname;
        if (it != quotes.end()) {
          px = it->second.price;
          qname = it->second.name;
        }
        if (a.qty_unit == "amount") {
          qty = px > 0 ? (int)std::llround(a.qty / px) : 0;
        }
        qty = floor_lot(qty, code);
        std::string err;
        if (qty > 0 && px > 0) {
          emit_trade(code, a.side, qty, px, "alert_file:" + a.name, qname, err);
          if (!log_line.empty()) log_line += " | ";
          log_line += "file-alert " + a.name + " " + a.side + " " + code;
        }
      }
      if (a.once) {
        a.enabled = false;
        a.status = "done";
      } else {
        a.status = "watching";
      }
    }
  }
  if (dirty) persist_alerts_locked();
}

void ProductEngine::eval_price_alerts(const std::unordered_map<std::string, grid::Quote>& quotes,
                                      std::string& log_line) {
  bool dirty = false;
  for (auto& a : alerts_) {
    if (!a.enabled) continue;
    if (a.source != "price" && a.source != "pct") continue;
    auto it = quotes.find(upper(a.code));
    if (it == quotes.end() || it->second.price <= 0) continue;
    double px = it->second.price;
    bool hit = false;
    if (a.source == "pct") {
      double ref = a.ref_price > 0 ? a.ref_price : px;
      if (a.ref_price <= 0) {
        a.ref_price = px;
        dirty = true;
        continue;
      }
      double pct = (px - ref) / ref * 100.0;
      if (a.compare == "lte")
        hit = pct <= -std::abs(a.trigger_pct);
      else
        hit = pct >= std::abs(a.trigger_pct);
    } else {
      hit = price_hit(a.compare, px, a.trigger_price);
    }
    if (!hit) continue;
    int64_t t = now_ms();
    if (a.cooldown_sec > 0 && a.last_trigger_ms > 0 && t - a.last_trigger_ms < a.cooldown_sec * 1000LL) continue;

    a.last_trigger_at = now_str();
    a.last_trigger_ms = t;
    a.trigger_count += 1;
    dirty = true;
    sound_armed_ = true;
    append_event("alert", a.name + " " + a.code + " @" + std::to_string(px), {{"side", a.side}});

    if (a.auto_trade && a.side != "notify") {
      int qty = (int)std::llround(a.qty_unit == "amount" ? (a.qty / px) : a.qty);
      qty = floor_lot(qty, a.code);
      std::string err;
      if (qty > 0 && emit_trade(a.code, a.side, qty, px, "alert:" + a.name, it->second.name, err)) {
        if (!log_line.empty()) log_line += " | ";
        log_line += "alert " + a.code + " " + a.side;
      } else if (!err.empty()) {
        append_event("alert_block", err, {{"id", a.id}});
      }
    }
    if (a.once) {
      a.enabled = false;
      a.status = "done";
    }
  }
  if (dirty) persist_alerts_locked();
}

void ProductEngine::eval_stops(const std::unordered_map<std::string, grid::Quote>& quotes, std::string& log_line) {
  bool dirty = false;
  for (auto& s : stops_) {
    if (!s.enabled || s.status == "triggered") continue;
    auto it = quotes.find(upper(s.code));
    if (it == quotes.end() || it->second.price <= 0) continue;
    double px = it->second.price;
    if (s.high_water <= 0 || px > s.high_water) {
      s.high_water = px;
      dirty = true;
    }

    bool stop = false;
    bool take = false;
    std::string reason;

    if (s.stop_price > 0 && px <= s.stop_price) {
      stop = true;
      reason = "stoploss_price";
    } else if (s.cost_price > 0 && s.stop_pct > 0) {
      double pct = (px - s.cost_price) / s.cost_price * 100.0;
      if (pct <= -std::abs(s.stop_pct)) {
        stop = true;
        reason = "stoploss";
      }
    }

    if (!stop) {
      if (s.trail_enabled && s.trail_pct > 0 && s.high_water > 0) {
        double drop = (s.high_water - px) / s.high_water * 100.0;
        if (drop >= std::abs(s.trail_pct)) {
          take = true;
          reason = "trail_take";
        }
      }
      if (!take && s.take_price > 0 && px >= s.take_price) {
        take = true;
        reason = "takeprofit_price";
      }
      if (!take && s.cost_price > 0 && s.take_pct > 0) {
        double pct = (px - s.cost_price) / s.cost_price * 100.0;
        if (pct >= std::abs(s.take_pct)) {
          take = true;
          reason = "takeprofit";
        }
      }
    }

    if (!stop && !take) continue;
    if (reason.empty()) reason = stop ? "stoploss" : "takeprofit";

    int qty = floor_lot(s.qty, s.code);
    std::string err;
    if (qty > 0 && emit_trade(s.code, "sell", qty, px, reason, s.name.empty() ? it->second.name : s.name, err)) {
      s.status = "triggered";
      s.enabled = false;
      s.last_trigger_at = now_str();
      dirty = true;
      sound_armed_ = true;
      if (!log_line.empty()) log_line += " | ";
      log_line += reason + " " + s.code;
      append_event(reason, s.code, {{"price", px}, {"high_water", s.high_water}});
    } else if (err == "need_confirm") {
      // keep watching until user confirms; do not disable
      dirty = true;
      sound_armed_ = true;
    }
  }
  if (dirty) persist_stops_locked();
}

void ProductEngine::eval_conds(const std::unordered_map<std::string, grid::Quote>& quotes, std::string& log_line) {
  bool dirty = false;
  for (auto& c : conds_) {
    if (!c.enabled || c.status != "armed") continue;
    const std::string kind = c.kind.empty() ? "price" : c.kind;
    bool hit = false;
    double px = 0;
    std::string qname;

    if (kind == "time") {
      if (!time_reached(c.trigger_time)) continue;
      hit = true;
      auto it = quotes.find(upper(c.code));
      if (it != quotes.end()) {
        px = c.limit_price > 0 ? c.limit_price : quote_price_by_type(it->second, c.price_type);
        qname = it->second.name;
      } else if (c.limit_price > 0) {
        px = c.limit_price;
      }
    } else {
      auto it = quotes.find(upper(c.code));
      if (it == quotes.end() || it->second.price <= 0) continue;
      qname = it->second.name;
      if (kind == "pct") {
        if (c.day_anchor <= 0) {
          c.day_anchor = it->second.price;
          dirty = true;
          continue;
        }
        double pct = (it->second.price - c.day_anchor) / c.day_anchor * 100.0;
        if (c.compare == "lte")
          hit = pct <= -std::abs(c.trigger_pct);
        else
          hit = pct >= std::abs(c.trigger_pct);
      } else {
        hit = price_hit(c.compare, it->second.price, c.trigger_price);
      }
      if (hit) px = c.limit_price > 0 ? c.limit_price : quote_price_by_type(it->second, c.price_type);
    }

    if (!hit || px <= 0) continue;
    int qty = floor_lot(c.qty, c.code);
    std::string err;
    if (qty > 0 && emit_trade(c.code, c.side, qty, px, "condition:" + kind, c.name.empty() ? qname : c.name, err,
                              order_price_type_for(c.limit_price > 0 ? "last" : c.price_type))) {
      c.status = "filled";
      c.enabled = false;
      c.last_trigger_at = now_str();
      dirty = true;
      sound_armed_ = true;
      if (!log_line.empty()) log_line += " | ";
      log_line += "cond " + c.code + " " + c.side + " " + kind;
    } else if (err == "need_confirm") {
      dirty = true;
      sound_armed_ = true;
    }
  }
  if (dirty) persist_conds_locked();
}

void ProductEngine::eval_watch_bands(const std::unordered_map<std::string, grid::Quote>& quotes,
                                     std::string& log_line) {
  for (auto& w : watch_) {
    auto it = quotes.find(upper(w.code));
    if (it == quotes.end()) continue;
    if (w.name.empty() && !it->second.name.empty()) {
      w.name = it->second.name;
      persist_watch_locked();
    }
    double px = it->second.price;
    if (w.alert_high > 0 && px >= w.alert_high) {
      append_event("watch_high", w.code + " >= " + std::to_string(w.alert_high), {{"price", px}});
      sound_armed_ = true;
      w.alert_high = 0;  // one-shot band
      persist_watch_locked();
      if (!log_line.empty()) log_line += " | ";
      log_line += "watch-high " + w.code;
    }
    if (w.alert_low > 0 && px <= w.alert_low) {
      append_event("watch_low", w.code + " <= " + std::to_string(w.alert_low), {{"price", px}});
      sound_armed_ = true;
      w.alert_low = 0;
      persist_watch_locked();
      if (!log_line.empty()) log_line += " | ";
      log_line += "watch-low " + w.code;
    }
  }
}

void ProductEngine::tick(std::string& log_line) {
  log_line.clear();
  sound_armed_ = false;
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  refresh_day_equity_locked();

  if (risk_.trade_days_only && !is_a_share_trading_day()) {
    log_line = "product idle: weekend/holiday";
    return;
  }

  if (!risk_state_.paused_loss) scan_file_signals(log_line);

  std::vector<std::string> codes;
  auto add = [&](const std::string& c) {
    if (c.empty()) return;
    auto u = upper(grid::normalize_code(c));
    if (std::find(codes.begin(), codes.end(), u) == codes.end()) codes.push_back(u);
  };
  for (const auto& a : alerts_)
    if (a.enabled && (a.source == "price" || a.source == "pct")) add(a.code);
  for (const auto& s : stops_)
    if (s.enabled) add(s.code);
  for (const auto& c : conds_)
    if (c.enabled && c.status == "armed") add(c.code);
  for (const auto& w : watch_) add(w.code);

  std::unordered_map<std::string, grid::Quote> quotes;
  if (!codes.empty()) {
    std::string err;
    auto src = SettingsStore::instance().load().quote_source;
    if (!grid::fetch_quotes(src.empty() ? "tencent" : src, codes, quotes, err)) {
      if (!log_line.empty()) log_line += " | ";
      log_line += "quote fail: " + err;
    } else if (!risk_state_.paused_loss) {
      eval_price_alerts(quotes, log_line);
      eval_stops(quotes, log_line);
      eval_conds(quotes, log_line);
      eval_watch_bands(quotes, log_line);
    }
  } else if (!risk_state_.paused_loss) {
    // time-only conditions still need a quote path — fetch their codes alone already covered;
    // if only time conds without quotes map entries, eval with empty map for time+limit_price
    bool has_time = false;
    for (const auto& c : conds_)
      if (c.enabled && c.status == "armed" && c.kind == "time") has_time = true;
    if (has_time) eval_conds(quotes, log_line);
  }

  if (risk_state_.paused_loss && log_line.empty()) log_line = "product paused: daily loss limit";

  if (sound_armed_) beep_if(risk_.sound_on_trigger);
  if (log_line.empty()) {
    log_line = "product heartbeat alerts=" + std::to_string(alerts_.size()) +
               " stops=" + std::to_string(stops_.size()) + " conds=" + std::to_string(conds_.size()) +
               " watch=" + std::to_string(watch_.size()) + " confirms=" + std::to_string(confirms_.size());
  }
}

}  // namespace desk
