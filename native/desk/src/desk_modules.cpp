#include "desk/desk_modules.hpp"

#include "desk/settings.hpp"
#include "grid/quote_factory.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>

namespace desk {
namespace {

std::string upper(std::string s) {
  for (auto& c : s) c = (char)toupper((unsigned char)c);
  return s;
}

}  // namespace

void DeskModules::init(const std::filesystem::path& data_dir, const std::filesystem::path& orders_dir) {
  std::lock_guard<std::mutex> lock(mu_);
  data_dir_ = data_dir;
  orders_dir_ = orders_dir;
  set_holiday_file(data_dir_ / "holidays.json");
  reload_locked();
  if (accounts_.empty()) {
    Account a;
    a.id = new_id();
    a.label = "默认纸面账户";
    a.broker_channel = "paper";
    a.capital = 1000000;
    a.cash = 1000000;
    a.equity = 1000000;
    accounts_.push_back(a);
    persist_all_locked();
  }
}

void DeskModules::reload_locked() {
  std::string err;
  nlohmann::json arr;
  t0_.clear();
  if (load_json_array(data_dir_ / "t0.json", arr, err))
    for (const auto& j : arr) t0_.push_back(t0_from_json(j));
  baskets_.clear();
  if (load_json_array(data_dir_ / "baskets.json", arr, err))
    for (const auto& j : arr) baskets_.push_back(basket_from_json(j));
  boards_.clear();
  if (load_json_array(data_dir_ / "board.json", arr, err))
    for (const auto& j : arr) boards_.push_back(board_from_json(j));
  formulas_.clear();
  if (load_json_array(data_dir_ / "formulas.json", arr, err))
    for (const auto& j : arr) formulas_.push_back(formula_from_json(j));
  accounts_.clear();
  if (load_json_array(data_dir_ / "accounts.json", arr, err))
    for (const auto& j : arr) accounts_.push_back(account_from_json(j));
  curve_.clear();
  if (load_json_array(data_dir_ / "equity_curve.json", arr, err)) {
    for (const auto& j : arr) {
      CurvePoint p;
      p.day = j.value("day", "");
      p.equity = j.value("equity", 0.0);
      p.cash = j.value("cash", 0.0);
      curve_.push_back(p);
    }
  }
  load_json_array(data_dir_ / "templates.json", templates_, err);
  if (!templates_.is_array()) templates_ = nlohmann::json::array();
}

void DeskModules::persist_all_locked() {
  std::string err;
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& x : t0_) arr.push_back(t0_to_json(x));
  save_json_array(data_dir_ / "t0.json", arr, err);
  arr = nlohmann::json::array();
  for (const auto& x : baskets_) arr.push_back(basket_to_json(x));
  save_json_array(data_dir_ / "baskets.json", arr, err);
  arr = nlohmann::json::array();
  for (const auto& x : boards_) arr.push_back(board_to_json(x));
  save_json_array(data_dir_ / "board.json", arr, err);
  arr = nlohmann::json::array();
  for (const auto& x : formulas_) arr.push_back(formula_to_json(x));
  save_json_array(data_dir_ / "formulas.json", arr, err);
  arr = nlohmann::json::array();
  for (const auto& x : accounts_) arr.push_back(account_to_json(x));
  save_json_array(data_dir_ / "accounts.json", arr, err);
  arr = nlohmann::json::array();
  for (const auto& x : curve_) arr.push_back({{"day", x.day}, {"equity", x.equity}, {"cash", x.cash}});
  save_json_array(data_dir_ / "equity_curve.json", arr, err);
  save_json_array(data_dir_ / "templates.json", templates_, err);
}

#define LIST_IMPL(name, vec, toj)        \
  nlohmann::json DeskModules::list_##name() { \
    std::lock_guard<std::mutex> lock(mu_);    \
    reload_locked();                          \
    nlohmann::json arr = nlohmann::json::array(); \
    for (const auto& x : vec) arr.push_back(toj(x)); \
    return arr;                               \
  }

LIST_IMPL(t0, t0_, t0_to_json)
LIST_IMPL(baskets, baskets_, basket_to_json)
LIST_IMPL(boards, boards_, board_to_json)
LIST_IMPL(formulas, formulas_, formula_to_json)
LIST_IMPL(accounts, accounts_, account_to_json)

bool DeskModules::upsert_t0(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  T0Rule r = t0_from_json(body);
  if (r.id.empty()) r.id = new_id();
  r.code = upper(r.code);
  if (r.code.empty()) {
    err = "code required";
    return false;
  }
  if (body.contains("qty") && !body.contains("buy_qty")) r.buy_qty = body.value("qty", r.buy_qty);
  r.qty = r.buy_qty;
  if (body.contains("base_price")) {
    r.base_price = body.value("base_price", 0.0);
    if (r.base_price > 0) r.day_anchor = r.base_price;
  } else if (body.contains("day_anchor")) {
    r.base_price = body.value("day_anchor", 0.0);
    r.day_anchor = r.base_price;
  }
  bool found = false;
  for (auto& x : t0_)
    if (x.id == r.id) {
      if (!body.contains("day_anchor") && !body.contains("base_price")) {
        r.day_anchor = x.day_anchor;
        r.base_price = x.base_price;
      }
      if (!body.contains("long_today")) r.long_today = x.long_today;
      if (!body.contains("rounds_today")) r.rounds_today = x.rounds_today;
      if (!body.contains("day_key")) r.day_key = x.day_key;
      if (!body.contains("last_action_ms")) r.last_action_ms = x.last_action_ms;
      if (!body.contains("last_trigger_at")) r.last_trigger_at = x.last_trigger_at;
      if (!body.contains("status") || body.value("status", "").empty()) r.status = x.status;
      x = r;
      found = true;
      break;
    }
  if (!found) {
    if (r.base_price > 0 && r.day_anchor <= 0) r.day_anchor = r.base_price;
    t0_.push_back(r);
  }
  persist_all_locked();
  out = t0_to_json(r);
  return true;
}
bool DeskModules::delete_t0(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto n = t0_.size();
  t0_.erase(std::remove_if(t0_.begin(), t0_.end(), [&](const T0Rule& r) { return r.id == id; }), t0_.end());
  if (t0_.size() == n) {
    err = "not found";
    return false;
  }
  persist_all_locked();
  return true;
}
bool DeskModules::toggle_t0(const std::string& id, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  for (auto& r : t0_) {
    if (r.id != id) continue;
    r.enabled = !r.enabled;
    persist_all_locked();
    out = t0_to_json(r);
    return true;
  }
  err = "not found";
  return false;
}

bool DeskModules::upsert_basket(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  Basket b = basket_from_json(body);
  if (b.id.empty()) b.id = new_id();
  bool found = false;
  for (auto& x : baskets_)
    if (x.id == b.id) {
      x = b;
      found = true;
      break;
    }
  if (!found) baskets_.push_back(b);
  persist_all_locked();
  out = basket_to_json(b);
  return true;
}
bool DeskModules::delete_basket(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto n = baskets_.size();
  baskets_.erase(std::remove_if(baskets_.begin(), baskets_.end(), [&](const Basket& b) { return b.id == id; }),
                 baskets_.end());
  if (baskets_.size() == n) {
    err = "not found";
    return false;
  }
  persist_all_locked();
  return true;
}
bool DeskModules::run_basket(const std::string& id, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  Basket* b = nullptr;
  for (auto& x : baskets_)
    if (x.id == id) b = &x;
  if (!b) {
    err = "not found";
    return false;
  }
  std::vector<std::string> codes;
  for (const auto& l : b->legs) codes.push_back(upper(l.code));
  std::unordered_map<std::string, grid::Quote> quotes;
  std::string qerr;
  auto src = SettingsStore::instance().load().quote_source;
  grid::fetch_quotes(src.empty() ? "tencent" : src, codes, quotes, qerr);

  nlohmann::json results = nlohmann::json::array();
  // weight allocation
  double tw = 0;
  for (const auto& l : b->legs)
    if (l.weight > 0) tw += l.weight;

  for (const auto& l : b->legs) {
    auto code = upper(l.code);
    auto it = quotes.find(code);
    double px = it != quotes.end() ? it->second.price : 0;
    int qty = l.qty;
    if (tw > 0 && b->capital > 0 && l.weight > 0 && px > 0) {
      qty = floor_lot((int)std::llround(b->capital * (l.weight / tw) / px), code);
    }
    qty = floor_lot(qty, code);
    nlohmann::json one{{"code", code}, {"side", l.side}, {"qty", qty}, {"price", px}};
    if (qty <= 0 || px <= 0) {
      one["ok"] = false;
      one["err"] = "no qty/price";
    } else {
      // emit via temporary - run_basket called from AppState with emit; store pending
      one["ok"] = true;
      one["pending"] = true;
    }
    results.push_back(one);
  }
  out = {{"basket_id", id}, {"legs", results}};
  // mark legs needing emit in out for http layer
  return true;
}

bool DeskModules::upsert_board(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  BoardRule b = board_from_json(body);
  if (b.id.empty()) b.id = new_id();
  b.code = upper(b.code);
  bool found = false;
  for (auto& x : boards_)
    if (x.id == b.id) {
      x = b;
      found = true;
      break;
    }
  if (!found) boards_.push_back(b);
  persist_all_locked();
  out = board_to_json(b);
  return true;
}
bool DeskModules::delete_board(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto n = boards_.size();
  boards_.erase(std::remove_if(boards_.begin(), boards_.end(), [&](const BoardRule& b) { return b.id == id; }),
                boards_.end());
  if (boards_.size() == n) {
    err = "not found";
    return false;
  }
  persist_all_locked();
  return true;
}
bool DeskModules::toggle_board(const std::string& id, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  for (auto& b : boards_) {
    if (b.id != id) continue;
    b.enabled = !b.enabled;
    persist_all_locked();
    out = board_to_json(b);
    return true;
  }
  err = "not found";
  return false;
}

bool DeskModules::upsert_formula(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  FormulaJob f = formula_from_json(body);
  if (f.id.empty()) f.id = new_id();
  bool found = false;
  for (auto& x : formulas_)
    if (x.id == f.id) {
      x = f;
      found = true;
      break;
    }
  if (!found) formulas_.push_back(f);
  persist_all_locked();
  out = formula_to_json(f);
  return true;
}
bool DeskModules::delete_formula(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto n = formulas_.size();
  formulas_.erase(std::remove_if(formulas_.begin(), formulas_.end(), [&](const FormulaJob& f) { return f.id == id; }),
                  formulas_.end());
  if (formulas_.size() == n) {
    err = "not found";
    return false;
  }
  persist_all_locked();
  return true;
}

bool DeskModules::upsert_account(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  Account a = account_from_json(body);
  if (a.id.empty()) a.id = new_id();
  if (a.label.empty()) a.label = a.id;
  bool found = false;
  for (auto& x : accounts_)
    if (x.id == a.id) {
      x = a;
      found = true;
      break;
    }
  if (!found) accounts_.push_back(a);
  persist_all_locked();
  out = account_to_json(a);
  return true;
}
bool DeskModules::delete_account(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  if (accounts_.size() <= 1) {
    err = "keep at least one account";
    return false;
  }
  auto n = accounts_.size();
  accounts_.erase(std::remove_if(accounts_.begin(), accounts_.end(), [&](const Account& a) { return a.id == id; }),
                  accounts_.end());
  if (accounts_.size() == n) {
    err = "not found";
    return false;
  }
  persist_all_locked();
  return true;
}

nlohmann::json DeskModules::curve_json() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& p : curve_) arr.push_back({{"day", p.day}, {"equity", p.equity}, {"cash", p.cash}});
  double eq = accounts_.empty() ? 0 : accounts_.front().equity;
  return {{"points", arr}, {"equity", eq}, {"accounts", accounts_.size()}};
}

nlohmann::json DeskModules::list_templates() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  return templates_;
}
bool DeskModules::save_template(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  nlohmann::json t = body;
  if (!t.contains("id") || t["id"].get<std::string>().empty()) t["id"] = new_id();
  if (!t.contains("name") || t["name"].get<std::string>().empty()) t["name"] = "template";
  if (!t.contains("kind")) t["kind"] = "grid";
  if (!t.contains("is_default")) t["is_default"] = false;
  if (t.value("is_default", false)) {
    const auto kind = t.value("kind", "grid");
    for (auto& x : templates_) {
      if (x.value("kind", "") == kind && x.value("id", "") != t.value("id", "")) x["is_default"] = false;
    }
  }
  bool found = false;
  for (auto& x : templates_) {
    if (x.value("id", "") == t.value("id", "")) {
      x = t;
      found = true;
      break;
    }
  }
  if (!found) templates_.push_back(t);
  persist_all_locked();
  out = t;
  return true;
}
bool DeskModules::delete_template(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  nlohmann::json next = nlohmann::json::array();
  bool found = false;
  for (const auto& x : templates_) {
    if (x.value("id", "") == id) {
      found = true;
      continue;
    }
    next.push_back(x);
  }
  if (!found) {
    err = "not found";
    return false;
  }
  templates_ = next;
  persist_all_locked();
  return true;
}

void DeskModules::tick(
    std::function<bool(const std::string&, const std::string&, int, double, const std::string&, const std::string&,
                       std::string&, const std::string&)>
        emit,
    const std::function<void(const std::string&, const std::string&)>& event, std::string& log_line) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();

  // equity snapshot once per day
  std::string day = today_ymd();
  if (curve_day_ != day && !accounts_.empty()) {
    curve_day_ = day;
    double eq = 0, cash = 0;
    for (const auto& a : accounts_)
      if (a.enabled) {
        eq += a.equity;
        cash += a.cash;
      }
    bool has = false;
    for (auto& p : curve_)
      if (p.day == day) {
        p.equity = eq;
        p.cash = cash;
        has = true;
      }
    if (!has) curve_.push_back(CurvePoint{day, eq, cash});
    while (curve_.size() > 400) curve_.erase(curve_.begin());
    persist_all_locked();
  }

  std::vector<std::string> codes;
  auto add = [&](const std::string& c) {
    auto u = upper(c);
    if (u.empty()) return;
    if (std::find(codes.begin(), codes.end(), u) == codes.end()) codes.push_back(u);
  };
  for (const auto& r : t0_)
    if (r.enabled) add(r.code);
  for (const auto& b : boards_)
    if (b.enabled) add(b.code);

  std::unordered_map<std::string, grid::Quote> quotes;
  if (!codes.empty()) {
    std::string qerr;
    auto src = SettingsStore::instance().load().quote_source;
    grid::fetch_quotes(src.empty() ? "tencent" : src, codes, quotes, qerr);
  }

  bool dirty = false;
  // T+0: buy on dip from day_anchor, sell on rise if long; daily reset + cooldown + rounds
  for (auto& r : t0_) {
    if (!r.enabled) continue;
    if (r.day_key != day) {
      r.day_key = day;
      r.day_anchor = r.base_price > 0 ? r.base_price : 0;
      r.long_today = false;
      r.rounds_today = 0;
      r.last_action_ms = 0;
      r.status = "watching";
      dirty = true;
    }
    if (r.session_only && !in_session_hms("09:25:00", "15:00:00")) {
      if (r.status != "session_wait" && r.status != "done_today") {
        r.status = "session_wait";
        dirty = true;
      }
      continue;
    }
    if (r.max_rounds > 0 && r.rounds_today >= r.max_rounds && !r.long_today) {
      if (r.status != "done_today") {
        r.status = "done_today";
        dirty = true;
      }
      continue;
    }
    auto it = quotes.find(upper(r.code));
    if (it == quotes.end() || it->second.price <= 0) continue;
    double px = it->second.price;
    if (r.day_anchor <= 0) {
      r.day_anchor = px;
      r.status = "watching";
      dirty = true;
      continue;
    }
    const int64_t t = now_ms();
    if (r.cooldown_sec > 0 && r.last_action_ms > 0 && t - r.last_action_ms < r.cooldown_sec * 1000LL) {
      continue;
    }
    const int buy_qty = floor_lot(r.buy_qty > 0 ? r.buy_qty : r.qty, r.code);
    const int sell_qty = floor_lot(r.sell_qty > 0 ? r.sell_qty : (r.buy_qty > 0 ? r.buy_qty : r.qty), r.code);
    const std::string ptype = order_price_type_for(r.order_type);
    const double order_px = quote_price_by_type(it->second, r.order_type);
    if (!r.long_today) {
      if (px <= r.day_anchor * (1.0 - std::abs(r.buy_down_pct) / 100.0)) {
        std::string e;
        if (buy_qty > 0 && emit(r.code, "buy", buy_qty, order_px > 0 ? order_px : px, "t0_buy", r.name, e, ptype)) {
          r.long_today = true;
          r.status = "long";
          r.last_action_ms = t;
          r.last_trigger_at = now_str();
          dirty = true;
          event("t0", "BUY " + r.code + " x" + std::to_string(buy_qty) + " " + ptype);
          if (!log_line.empty()) log_line += " | ";
          log_line += "t0-buy " + r.code;
        }
      } else if (r.status != "watching" && r.status != "done_today") {
        r.status = "watching";
        dirty = true;
      }
    } else {
      if (px >= r.day_anchor * (1.0 + std::abs(r.sell_up_pct) / 100.0)) {
        std::string e;
        if (sell_qty > 0 &&
            emit(r.code, "sell", sell_qty, order_px > 0 ? order_px : px, "t0_sell", r.name, e, ptype)) {
          r.long_today = false;
          r.day_anchor = px;
          r.rounds_today += 1;
          r.last_action_ms = t;
          r.last_trigger_at = now_str();
          r.status = (r.max_rounds > 0 && r.rounds_today >= r.max_rounds) ? "done_today" : "watching";
          dirty = true;
          event("t0", "SELL " + r.code + " x" + std::to_string(sell_qty) + " " + ptype + " round=" +
                          std::to_string(r.rounds_today));
          if (!log_line.empty()) log_line += " | ";
          log_line += "t0-sell " + r.code;
        }
      } else if (r.status != "long") {
        r.status = "long";
        dirty = true;
      }
    }
  }

  // Board: approximate day change using quote fields — if only last price, skip unless we have open.
  // Use eastmoney-less path: store first price as open proxy in day_anchor via BoardRule.day_fired empty reset.
  for (auto& b : boards_) {
    if (!b.enabled) continue;
    if (b.day_fired == day) continue;
    auto it = quotes.find(upper(b.code));
    if (it == quotes.end() || it->second.price <= 0) continue;
    // Without prev close, use seal as absolute: if name empty fetch; trigger if ask starved — simplified:
    // treat seal_pct as "price rose X% from session first seen"
    static thread_local std::unordered_map<std::string, double> open_px;
    auto key = upper(b.code);
    if (!open_px.count(key)) open_px[key] = it->second.price;
    double op = open_px[key];
    if (op <= 0) continue;
    double pct = (it->second.price - op) / op * 100.0;
    if (pct < b.seal_pct) continue;
    int qty = floor_lot(b.qty, b.code);
    std::string e;
    if (qty > 0 && emit(b.code, "buy", qty, it->second.price, "board", b.name, e, "limit")) {
      b.day_fired = day;
      b.last_trigger_at = now_str();
      b.status = "fired";
      dirty = true;
      event("board", b.code + " +" + std::to_string(pct) + "%");
      if (!log_line.empty()) log_line += " | ";
      log_line += "board " + b.code;
    }
  }

  // Formula jobs: scan tdx sign file using risk path from ProductEngine — read settings risk file
  auto risk_path = data_dir_ / "risk.json";
  nlohmann::json risk;
  std::string err;
  load_json_object(risk_path, risk, err);
  std::string sign = risk.value("tdx_sign_file", "");
  if (!sign.empty() && std::filesystem::exists(std::filesystem::u8path(sign))) {
    std::ifstream in(std::filesystem::u8path(sign));
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    std::string fp = std::to_string(content.size()) + ":" +
                     (content.size() > 60 ? content.substr(content.size() - 60) : content);
    if (fp != last_sign_fp_) {
      last_sign_fp_ = fp;
      for (auto& f : formulas_) {
        if (!f.enabled || f.name.empty()) continue;
        if (content.find(f.name) == std::string::npos) continue;
        // extract code
        std::string code;
        for (size_t i = 0; i + 6 <= content.size(); ++i) {
          bool dig = true;
          for (int k = 0; k < 6; ++k)
            if (!std::isdigit((unsigned char)content[i + k])) {
              dig = false;
              break;
            }
          if (dig) {
            std::string num = content.substr(i, 6);
            code = (num[0] == '6' || num[0] == '5') ? num + ".SH" : num + ".SZ";
            break;
          }
        }
        if (code.empty()) continue;
        std::unordered_map<std::string, grid::Quote> q2;
        std::string qe;
        auto src = SettingsStore::instance().load().quote_source;
        grid::fetch_quotes(src.empty() ? "tencent" : src, {code}, q2, qe);
        double px = q2.count(code) ? q2[code].price : 0;
        int qty = floor_lot((int)std::llround(f.qty_unit == "amount" && px > 0 ? f.qty / px : f.qty), code);
        std::string e;
        if (qty > 0 && px > 0 && emit(code, f.side, qty, px, "formula:" + f.name, "", e, "limit")) {
          f.trigger_count += 1;
          f.last_trigger_at = now_str();
          dirty = true;
          event("formula", f.name + " " + code);
          if (!log_line.empty()) log_line += " | ";
          log_line += "formula " + f.name;
        }
      }
    }
  }

  // paper equity mark: bump equity slightly with random walk disabled — keep cash=equity if no positions tracked
  if (dirty) persist_all_locked();
}

}  // namespace desk
