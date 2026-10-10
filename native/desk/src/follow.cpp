#include "desk/follow.hpp"

#include "desk/portfolios.hpp"
#include "desk/settings.hpp"
#include "grid/http_win.hpp"
#include "grid/order_file.hpp"
#include "grid/quote_factory.hpp"

#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace desk {
namespace {

std::mutex g_mu;

std::filesystem::path follow_json_path() {
  auto orders = SettingsStore::instance().orders_root();
  return orders.parent_path() / "follow.json";
}

std::string now_str() {
  using clock = std::chrono::system_clock;
  auto t = clock::to_time_t(clock::now());
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

int lot_of(const std::string& code) {
  auto num = code.substr(0, code.find('.'));
  if (num.size() >= 2 && (num.substr(0, 2) == "11" || num.substr(0, 2) == "12")) return 10;
  return 100;
}

int floor_lot(double shares, const std::string& code) {
  int lot = lot_of(code);
  int q = (int)std::floor(shares / lot) * lot;
  return q < 0 ? 0 : q;
}

nlohmann::json load_follow() {
  auto path = follow_json_path();
  if (!std::filesystem::exists(path)) return nullptr;
  try {
    std::ifstream in(path);
    nlohmann::json j;
    in >> j;
    return j;
  } catch (...) {
    return nullptr;
  }
}

void save_follow(const nlohmann::json& j) {
  auto path = follow_json_path();
  auto tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << j.dump(2);
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::rename(tmp, path, ec);
}

void append_log(nlohmann::json& state, const std::string& result, const std::string& message) {
  nlohmann::json entry = {{"time", now_str()}, {"result", result}, {"message", message}};
  auto logs = state.value("logs", nlohmann::json::array());
  nlohmann::json next = nlohmann::json::array();
  next.push_back(entry);
  for (size_t i = 0; i < logs.size() && i < 99; ++i) next.push_back(logs[i]);
  state["logs"] = next;
}

nlohmann::json fetch_target(const std::string& source, const std::string& id, std::string& err) {
  int status = 200;
  std::string body = portfolio_detail_json(source, id, status, err);
  if (status != 200) return nullptr;
  try {
    return nlohmann::json::parse(body);
  } catch (...) {
    err = "parse portfolio detail";
    return nullptr;
  }
}

void mark_to_market(nlohmann::json& state) {
  std::vector<std::string> codes;
  for (auto& p : state.value("positions", nlohmann::json::array())) {
    std::string c = grid::normalize_code(p.value("code", ""));
    p["code"] = c;
    codes.push_back(c);
  }
  for (auto& h : state.value("target_holdings", nlohmann::json::array())) {
    std::string c = grid::normalize_code(h.value("code", ""));
    h["code"] = c;
    codes.push_back(c);
  }
  auto settings = SettingsStore::instance().load();
  std::unordered_map<std::string, grid::Quote> quotes;
  std::string err;
  grid::fetch_quotes(settings.quote_source, codes, quotes, err);
  double equity = 0;
  for (auto& pos : state["positions"]) {
    std::string code = pos.value("code", "");
    double px = pos.value("price", 0.0);
    auto it = quotes.find(code);
    if (it != quotes.end() && it->second.price > 0) {
      px = it->second.price;
      pos["price"] = px;
      if (!it->second.name.empty()) pos["name"] = it->second.name;
    }
    int shares = pos.value("shares", 0);
    double cost = pos.value("cost", 0.0);
    pos["market_value"] = std::round(shares * px * 100) / 100.0;
    pos["pnl"] = std::round((px - cost) * shares * 100) / 100.0;
    pos["pnl_pct"] = (cost > 0 && shares > 0) ? std::round(pos["pnl"].get<double>() / (cost * shares) * 10000) / 100.0 : 0;
    equity += pos["market_value"].get<double>();
  }
  double cash = state.value("cash", 0.0);
  double nav = cash + equity;
  double initial = state.value("initial", nav);
  state["nav"] = std::round(nav * 100) / 100.0;
  state["pnl"] = std::round((nav - initial) * 100) / 100.0;
  state["pnl_pct"] = initial > 0 ? std::round(state["pnl"].get<double>() / initial * 10000) / 100.0 : 0;
  state["updated_at"] = now_str();
  for (auto& pos : state["positions"]) {
    pos["weight"] = nav > 0 ? std::round(pos["market_value"].get<double>() / nav * 10000) / 100.0 : 0;
  }
}

void emit_orders(nlohmann::json& actions) {
  auto settings = SettingsStore::instance().load();
  if (settings.broker_channel == "paper" || settings.broker_channel.empty()) return;
  auto root = SettingsStore::instance().orders_root();
  for (auto& action : actions) {
    grid::OrderIntent intent;
    intent.code = action.value("code", "");
    std::string side = action.value("side", "");
    intent.side = (side.find("买") != std::string::npos || side == "buy") ? "buy" : "sell";
    intent.qty = action.value("shares", 0);
    intent.price = action.value("price", 0.0);
    intent.name = action.value("name", "");
    intent.reason = action.value("reason", "");
    std::string err;
    grid::write_order_json(root, intent, err);
  }
}

bool rebalance(nlohmann::json& state, const nlohmann::json& target, std::string& err) {
  auto holdings = target.value("holdings", nlohmann::json::array());
  std::vector<std::string> codes;
  for (const auto& h : holdings) codes.push_back(h.value("code", ""));
  for (const auto& p : state.value("positions", nlohmann::json::array())) codes.push_back(p.value("code", ""));
  auto settings = SettingsStore::instance().load();
  std::unordered_map<std::string, grid::Quote> quotes;
  grid::fetch_quotes(settings.quote_source, codes, quotes, err);

  auto price_of = [&](const std::string& code, double fallback) {
    auto it = quotes.find(code);
    if (it != quotes.end() && it->second.price > 0) return it->second.price;
    return fallback;
  };

  mark_to_market(state);
  double nav = state.value("nav", state.value("cash", 0.0));
  double ratio = state.value("follow_ratio", 1.0);
  double budget = nav * ratio;
  nlohmann::json actions = nlohmann::json::array();

  // sells
  nlohmann::json new_pos = nlohmann::json::array();
  for (auto pos : state.value("positions", nlohmann::json::array())) {
    std::string code = pos.value("code", "");
    double tw = 0;
    for (const auto& h : holdings) {
      if (h.value("code", "") == code) {
        tw = h.value("weight", 0.0);
        break;
      }
    }
    double px = price_of(code, pos.value("price", pos.value("cost", 0.0)));
    if (px <= 0) {
      new_pos.push_back(pos);
      continue;
    }
    int target_shares = tw > 0 ? floor_lot(budget * tw / 100.0 / px, code) : 0;
    int have = pos.value("shares", 0);
    int delta = have - target_shares;
    if (delta >= lot_of(code)) {
      double proceeds = delta * px;
      state["cash"] = state.value("cash", 0.0) + proceeds;
      pos["shares"] = have - delta;
      actions.push_back({{"time", now_str()},
                         {"side", "卖出"},
                         {"code", code},
                         {"name", pos.value("name", "")},
                         {"shares", delta},
                         {"price", px},
                         {"amount", std::round(proceeds * 100) / 100.0},
                         {"reason", "对齐目标仓位"}});
    }
    if (pos.value("shares", 0) > 0) new_pos.push_back(pos);
  }
  state["positions"] = new_pos;

  // buys
  for (const auto& h : holdings) {
    double tw = h.value("weight", 0.0);
    if (tw <= 0) continue;
    std::string code = h.value("code", "");
    double px = price_of(code, h.value("price", 0.0));
    if (px <= 0) continue;
    int target_shares = floor_lot(budget * tw / 100.0 / px, code);
    int have = 0;
    nlohmann::json* cur = nullptr;
    for (auto& p : state["positions"]) {
      if (p.value("code", "") == code) {
        have = p.value("shares", 0);
        cur = &p;
        break;
      }
    }
    int delta = target_shares - have;
    if (delta < lot_of(code)) continue;
    double cost = delta * px;
    double cash = state.value("cash", 0.0);
    if (cost > cash) {
      delta = floor_lot(cash / px, code);
      cost = delta * px;
    }
    if (delta < lot_of(code)) continue;
    state["cash"] = cash - cost;
    if (cur) {
      double old_cost = cur->value("cost", px);
      int old_shares = cur->value("shares", 0);
      double total = old_cost * old_shares + cost;
      (*cur)["shares"] = old_shares + delta;
      (*cur)["cost"] = total / (*cur)["shares"].get<int>();
      (*cur)["price"] = px;
      (*cur)["name"] = h.value("name", cur->value("name", ""));
    } else {
      state["positions"].push_back({{"code", code},
                                    {"name", h.value("name", "")},
                                    {"shares", delta},
                                    {"cost", px},
                                    {"price", px},
                                    {"market_value", cost},
                                    {"weight", tw},
                                    {"pnl", 0},
                                    {"pnl_pct", 0}});
    }
    actions.push_back({{"time", now_str()},
                       {"side", "买入"},
                       {"code", code},
                       {"name", h.value("name", "")},
                       {"shares", delta},
                       {"price", px},
                       {"amount", std::round(cost * 100) / 100.0},
                       {"reason", "对齐目标仓位"}});
  }

  emit_orders(actions);
  state["cash"] = std::round(state.value("cash", 0.0) * 100) / 100.0;
  state["target_holdings"] = holdings;
  state["target_name"] = target.value("name", state.value("target_name", ""));
  state["target_owner"] = target.value("owner", "");
  state["last_sync_at"] = now_str();
  auto trades = actions;
  for (const auto& t : state.value("trades", nlohmann::json::array())) {
    if (trades.size() >= 200) break;
    trades.push_back(t);
  }
  state["trades"] = trades;
  state["last_actions"] = actions;
  mark_to_market(state);
  return true;
}

}  // namespace

nlohmann::json follow_snapshot_json() {
  std::lock_guard<std::mutex> lock(g_mu);
  auto state = load_follow();
  if (state.is_null()) return {{"active", false}, {"account", nullptr}};
  mark_to_market(state);
  save_follow(state);
  return {{"active", true}, {"account", state}};
}

bool follow_start(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(g_mu);
  double capital = body.value("capital", 0.0);
  double ratio = body.value("follow_ratio", 1.0);
  std::string source = body.value("source", "eastmoney");
  std::string pid = body.value("portfolio_id", "");
  if (capital <= 0) {
    err = "capital must be > 0";
    return false;
  }
  if (!(ratio > 0 && ratio <= 1)) {
    err = "follow_ratio must be in (0,1]";
    return false;
  }
  if (pid.empty()) {
    err = "portfolio_id required";
    return false;
  }
  auto target = fetch_target(source, pid, err);
  if (target.is_null()) return false;
  nlohmann::json state = {
      {"source", source},
      {"portfolio_id", target.value("id", pid)},
      {"target_name", target.value("name", "")},
      {"target_owner", target.value("owner", "")},
      {"source_url", target.value("source_url", "")},
      {"initial", std::round(capital * 100) / 100.0},
      {"cash", std::round(capital * 100) / 100.0},
      {"follow_ratio", ratio},
      {"positions", nlohmann::json::array()},
      {"trades", nlohmann::json::array()},
      {"logs", nlohmann::json::array()},
      {"started_at", now_str()},
      {"status", "running"},
  };
  if (!rebalance(state, target, err)) return false;
  append_log(state, "ok", "开始跟单 " + state.value("target_name", pid));
  save_follow(state);
  out = {{"active", true}, {"account", state}};
  return true;
}

bool follow_sync(nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto state = load_follow();
  if (state.is_null()) {
    err = "no active follow";
    return false;
  }
  if (state.value("status", "") == "paused") {
    err = "follow paused";
    return false;
  }
  auto target = fetch_target(state.value("source", ""), state.value("portfolio_id", ""), err);
  if (target.is_null()) return false;
  if (!rebalance(state, target, err)) return false;
  append_log(state, "ok",
             "同步 " + state.value("target_name", "") + "，成交 " +
                 std::to_string(state.value("last_actions", nlohmann::json::array()).size()) + " 笔");
  save_follow(state);
  out = {{"active", true}, {"account", state}};
  return true;
}

bool follow_pause(nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto state = load_follow();
  if (state.is_null()) {
    err = "no active follow";
    return false;
  }
  state["status"] = "paused";
  append_log(state, "ok", "已暂停");
  mark_to_market(state);
  save_follow(state);
  out = {{"active", true}, {"account", state}};
  return true;
}

bool follow_resume(nlohmann::json& out, std::string& err) {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    auto state = load_follow();
    if (state.is_null()) {
      err = "no active follow";
      return false;
    }
    state["status"] = "running";
    append_log(state, "ok", "已继续");
    save_follow(state);
  }
  // sync after unlock (sync takes lock itself)
  if (!follow_sync(out, err)) {
    // still resumed even if sync fails
    std::lock_guard<std::mutex> lock(g_mu);
    auto state = load_follow();
    mark_to_market(state);
    save_follow(state);
    out = {{"active", true}, {"account", state}};
    err.clear();
  }
  return true;
}

void follow_stop() {
  std::lock_guard<std::mutex> lock(g_mu);
  auto path = follow_json_path();
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

}  // namespace desk
