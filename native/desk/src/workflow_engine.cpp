#include "desk/workflow_engine.hpp"

#include "desk/product.hpp"
#include "desk/settings.hpp"
#include "grid/bars.hpp"
#include "grid/expr.hpp"
#include "grid/http_win.hpp"
#include "grid/quote_factory.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace desk {
namespace {

std::string upper(std::string s) {
  for (auto& c : s) c = (char)toupper((unsigned char)c);
  return s;
}

const WorkflowNode* find_node(const WorkflowProgram& p, const std::string& id);

bool has_conditions(const nlohmann::json& cfg) {
  return cfg.contains("conditions") && cfg["conditions"].is_array() && !cfg["conditions"].empty();
}

std::unordered_map<std::string, double> quote_env(const grid::Quote& q) {
  std::unordered_map<std::string, double> env;
  env["CLOSE"] = q.price;
  env["PRE"] = q.prev_close;
  env["OPEN"] = q.open;
  env["HIGH"] = q.high > 0 ? q.high : q.price;
  env["LOW"] = q.low > 0 ? q.low : q.price;
  env["VOL"] = q.volume;
  env["AMOUNT"] = q.amount;
  env["PCT"] = q.prev_close > 0 ? (q.price - q.prev_close) / q.prev_close * 100.0 : 0;
  for (int i = 0; i < 5; ++i) {
    env["BID" + std::to_string(i + 1)] = q.bids[i];
    env["ASK" + std::to_string(i + 1)] = q.asks[i];
    env["BIDVOL" + std::to_string(i + 1)] = q.bid_vols[i];
    env["ASKVOL" + std::to_string(i + 1)] = q.ask_vols[i];
  }
  auto ind = grid::indicators_for({}, q.code, 1);
  grid::fill_indicator_env(ind, env, 1);
  for (int m : {5, 15, 30, 60}) {
    auto snap = grid::indicators_for({}, q.code, m);
    grid::fill_indicator_env(snap, env, m);
  }
  return env;
}

bool conditions_pass(const nlohmann::json& cfg, const std::unordered_map<std::string, double>& env) {
  const std::string combine = cfg.value("combine", "all");
  int n = 0, pass = 0;
  for (const auto& c : cfg["conditions"]) {
    std::string expr = c.is_string() ? c.get<std::string>() : c.value("expr", "");
    if (expr.empty()) continue;
    ++n;
    bool ok = false;
    std::string err;
    if (grid::eval_expr(expr, env, ok, err) && ok) ++pass;
  }
  if (n == 0) return false;
  return combine == "any" ? pass > 0 : pass == n;
}

std::vector<std::string> alias_targets(const WorkflowProgram& p, const std::vector<std::string>& pools,
                                      const std::string& code) {
  std::vector<std::string> out;
  for (const auto& pid : pools) {
    const auto* node = find_node(p, pid);
    if (!node || !node->config.contains("aliases") || !node->config["aliases"].is_array()) continue;
    for (const auto& a : node->config["aliases"]) {
      std::string from = upper(grid::normalize_code(a.value("from", "")));
      if (from != code) continue;
      if (a.contains("to") && a["to"].is_array()) {
        for (const auto& t : a["to"]) {
          auto c = upper(grid::normalize_code(t.get<std::string>()));
          if (!c.empty()) out.push_back(c);
        }
      } else if (a.contains("to") && a["to"].is_string()) {
        auto c = upper(grid::normalize_code(a.value("to", "")));
        if (!c.empty()) out.push_back(c);
      }
    }
  }
  if (out.empty()) out.push_back(code);
  return out;
}

bool price_hit(const std::string& compare, double px, double trigger) {
  if (trigger <= 0 || px <= 0) return false;
  if (compare == "lte") return px <= trigger;
  return px >= trigger;
}

int parse_hms(const std::string& s) {
  int h = 0, m = 0, sec = 0;
#ifdef _WIN32
  if (sscanf_s(s.c_str(), "%d:%d:%d", &h, &m, &sec) >= 2) return h * 3600 + m * 60 + sec;
#else
  if (std::sscanf(s.c_str(), "%d:%d:%d", &h, &m, &sec) >= 2) return h * 3600 + m * 60 + sec;
#endif
  return -1;
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

std::string extract_code(const std::string& line) {
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

bool has_cycle(const WorkflowProgram& p) {
  std::unordered_map<std::string, std::vector<std::string>> adj;
  for (const auto& e : p.edges) adj[e.from].push_back(e.to);
  std::unordered_map<std::string, int> color;  // 0=white 1=gray 2=black
  std::function<bool(const std::string&)> dfs = [&](const std::string& u) -> bool {
    color[u] = 1;
    for (const auto& v : adj[u]) {
      if (color[v] == 1) return true;
      if (color[v] == 0 && dfs(v)) return true;
    }
    color[u] = 2;
    return false;
  };
  for (const auto& n : p.nodes) {
    if (color[n.id] == 0 && dfs(n.id)) return true;
  }
  return false;
}

const WorkflowNode* find_node(const WorkflowProgram& p, const std::string& id) {
  for (const auto& n : p.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

}  // namespace

nlohmann::json workflow_to_json(const WorkflowProgram& p) {
  nlohmann::json nodes = nlohmann::json::array();
  for (const auto& n : p.nodes) {
    nodes.push_back({{"id", n.id}, {"type", n.type}, {"x", n.x}, {"y", n.y}, {"config", n.config}});
  }
  nlohmann::json edges = nlohmann::json::array();
  for (const auto& e : p.edges) edges.push_back({{"from", e.from}, {"to", e.to}});
  return nlohmann::json{{"id", p.id},
                        {"name", p.name},
                        {"enabled", p.enabled},
                        {"pre_auth", p.pre_auth},
                        {"nodes", nodes},
                        {"edges", edges},
                        {"updated_at", p.updated_at},
                        {"note", p.note}};
}

WorkflowProgram workflow_from_json(const nlohmann::json& j) {
  WorkflowProgram p;
  p.id = j.value("id", "");
  p.name = j.value("name", "未命名程序");
  p.enabled = j.value("enabled", false);
  p.pre_auth = j.value("pre_auth", false);
  p.updated_at = j.value("updated_at", "");
  p.note = j.value("note", "");
  if (j.contains("nodes") && j["nodes"].is_array()) {
    for (const auto& n : j["nodes"]) {
      WorkflowNode node;
      node.id = n.value("id", "");
      node.type = n.value("type", "");
      node.x = n.value("x", 80.0);
      node.y = n.value("y", 80.0);
      node.config = n.value("config", nlohmann::json::object());
      if (!node.id.empty()) p.nodes.push_back(node);
    }
  }
  if (j.contains("edges") && j["edges"].is_array()) {
    for (const auto& e : j["edges"]) {
      WorkflowEdge edge;
      edge.from = e.value("from", "");
      edge.to = e.value("to", "");
      if (!edge.from.empty() && !edge.to.empty()) p.edges.push_back(edge);
    }
  }
  return p;
}

nlohmann::json workflow_list_item(const WorkflowProgram& p) {
  return nlohmann::json{{"id", p.id},
                        {"name", p.name},
                        {"enabled", p.enabled},
                        {"pre_auth", p.pre_auth},
                        {"nodes", (int)p.nodes.size()},
                        {"edges", (int)p.edges.size()},
                        {"updated_at", p.updated_at},
                        {"note", p.note}};
}

WorkflowProgram make_file_signal_template(const std::string& sign_name, double trigger_price) {
  WorkflowProgram p;
  p.id = new_id();
  p.name = "文件信号买入";
  p.enabled = false;
  p.pre_auth = false;
  p.updated_at = now_str();
  p.note = "信号进待买池 → 价格条件买入 → 持仓池";

  WorkflowNode sel;
  sel.id = new_id();
  sel.type = "selector";
  sel.x = 60;
  sel.y = 120;
  sel.config = {{"label", "文件选股"},
                {"source", "file"},
                {"sign_name", sign_name.empty() ? "买入信号" : sign_name},
                {"schedule", "every_poll"}};

  WorkflowNode pool_buy;
  pool_buy.id = new_id();
  pool_buy.type = "pool";
  pool_buy.x = 280;
  pool_buy.y = 120;
  pool_buy.config = {{"label", "待买池"}, {"codes", nlohmann::json::array()}, {"seed_from_watchlist", false}};

  WorkflowNode strat;
  strat.id = new_id();
  strat.type = "strategy";
  strat.x = 500;
  strat.y = 120;
  strat.config = {{"label", "价格买入"},
                  {"side", "buy"},
                  {"kind", "price"},
                  {"compare", "gte"},
                  {"trigger_price", trigger_price > 0 ? trigger_price : 0},
                  {"trigger_pct", 0},
                  {"trigger_time", ""},
                  {"qty", 100},
                  {"price_type", "ask1"},
                  {"limit_price", 0},
                  {"once_per_code_day", true}};

  WorkflowNode pool_hold;
  pool_hold.id = new_id();
  pool_hold.type = "pool";
  pool_hold.x = 720;
  pool_hold.y = 120;
  pool_hold.config = {{"label", "持仓池"}, {"codes", nlohmann::json::array()}, {"seed_from_watchlist", false}};

  p.nodes = {sel, pool_buy, strat, pool_hold};
  p.edges = {{sel.id, pool_buy.id}, {pool_buy.id, strat.id}, {strat.id, pool_hold.id}};
  return p;
}

void WorkflowEngine::init(const std::filesystem::path& data_dir) {
  std::lock_guard<std::mutex> lock(mu_);
  data_dir_ = data_dir;
  programs_path_ = data_dir_ / "workflows.json";
  state_path_ = data_dir_ / "workflow_state.json";
  reload_locked();
}

void WorkflowEngine::reload_locked() {
  programs_.clear();
  states_.clear();
  std::string err;
  nlohmann::json arr;
  if (load_json_array(programs_path_, arr, err)) {
    for (const auto& j : arr) programs_.push_back(workflow_from_json(j));
  }
  nlohmann::json st;
  if (load_json_object(state_path_, st, err) && st.is_object()) {
    for (auto it = st.begin(); it != st.end(); ++it) {
      WorkflowProgramState ps;
      const auto& obj = it.value();
      if (obj.contains("pools") && obj["pools"].is_object()) {
        for (auto pit = obj["pools"].begin(); pit != obj["pools"].end(); ++pit) {
          WorkflowPoolState pool;
          if (pit.value().is_array()) {
            for (const auto& c : pit.value()) pool.codes.insert(upper(c.get<std::string>()));
          }
          ps.pools[pit.key()] = std::move(pool);
        }
      }
      if (obj.contains("selector_last_run") && obj["selector_last_run"].is_object()) {
        for (auto sit = obj["selector_last_run"].begin(); sit != obj["selector_last_run"].end(); ++sit)
          ps.selector_last_run[sit.key()] = sit.value().get<std::string>();
      }
      if (obj.contains("strategy_fired") && obj["strategy_fired"].is_object()) {
        for (auto fit = obj["strategy_fired"].begin(); fit != obj["strategy_fired"].end(); ++fit) {
          std::unordered_set<std::string> set;
          if (fit.value().is_array())
            for (const auto& x : fit.value()) set.insert(x.get<std::string>());
          ps.strategy_fired[fit.key()] = std::move(set);
        }
      }
      if (obj.contains("recent") && obj["recent"].is_array()) ps.recent = obj["recent"];
      states_[it.key()] = std::move(ps);
    }
  }
}

void WorkflowEngine::persist_programs_locked() {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& p : programs_) arr.push_back(workflow_to_json(p));
  std::string err;
  save_json_array(programs_path_, arr, err);
}

void WorkflowEngine::persist_state_locked() {
  nlohmann::json root = nlohmann::json::object();
  for (const auto& kv : states_) {
    nlohmann::json pools = nlohmann::json::object();
    for (const auto& pk : kv.second.pools) {
      nlohmann::json codes = nlohmann::json::array();
      for (const auto& c : pk.second.codes) codes.push_back(c);
      pools[pk.first] = codes;
    }
    nlohmann::json sel = nlohmann::json::object();
    for (const auto& s : kv.second.selector_last_run) sel[s.first] = s.second;
    nlohmann::json fired = nlohmann::json::object();
    for (const auto& f : kv.second.strategy_fired) {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& x : f.second) arr.push_back(x);
      fired[f.first] = arr;
    }
    root[kv.first] = {{"pools", pools},
                      {"selector_last_run", sel},
                      {"strategy_fired", fired},
                      {"recent", kv.second.recent}};
  }
  std::string err;
  save_json_object(state_path_, root, err);
}

void WorkflowEngine::ensure_state_locked(const std::string& program_id) {
  auto& st = states_[program_id];
  auto* p = find_locked(program_id);
  if (!p) return;
  for (const auto& n : p->nodes) {
    if (n.type != "pool") continue;
    if (!st.pools.count(n.id)) {
      WorkflowPoolState pool;
      if (n.config.value("seed_from_watchlist", false)) {
        for (const auto& c : load_watchlist_codes()) pool.codes.insert(c);
      }
      if (n.config.contains("codes") && n.config["codes"].is_array()) {
        for (const auto& c : n.config["codes"]) {
          auto code = grid::normalize_code(c.get<std::string>());
          if (!code.empty()) pool.codes.insert(upper(code));
        }
      }
      st.pools[n.id] = std::move(pool);
    }
  }
}

WorkflowProgram* WorkflowEngine::find_locked(const std::string& id) {
  for (auto& p : programs_)
    if (p.id == id) return &p;
  return nullptr;
}

std::vector<std::string> WorkflowEngine::validate_program(const WorkflowProgram& p) const {
  std::vector<std::string> errors;
  if (p.name.empty()) errors.push_back("名称不能为空");
  if (p.nodes.empty()) errors.push_back("至少需要一个节点");
  std::unordered_set<std::string> ids;
  for (const auto& n : p.nodes) {
    if (n.id.empty()) errors.push_back("存在空节点 id");
    if (!ids.insert(n.id).second) errors.push_back("重复节点 id: " + n.id);
    if (n.type != "selector" && n.type != "pool" && n.type != "strategy")
      errors.push_back("未知节点类型: " + n.type);
    if (n.type == "selector") {
      auto src = n.config.value("source", "");
      if (src != "watchlist" && src != "file" && src != "quote")
        errors.push_back("选股器 " + n.id + " source 无效");
      if (src == "file" && n.config.value("sign_name", "").empty())
        errors.push_back("选股器 " + n.id + " 缺少 sign_name");
      if (src == "quote" && !has_conditions(n.config) && n.config.value("trigger_price", 0.0) <= 0 &&
          n.config.value("trigger_pct", 0.0) <= 0)
        errors.push_back("选股器 " + n.id + " 需设置 trigger_price、trigger_pct 或 conditions");
    }
    if (n.type == "strategy") {
      if (n.config.value("qty", 0) < 0) errors.push_back("策略单 " + n.id + " qty 无效");
      if (!has_conditions(n.config)) {
        auto kind = n.config.value("kind", "price");
        if (kind == "price" && n.config.value("trigger_price", 0.0) <= 0)
          errors.push_back("策略单 " + n.id + " 缺少 trigger_price");
        if (kind == "pct" && n.config.value("trigger_pct", 0.0) <= 0)
          errors.push_back("策略单 " + n.id + " 缺少 trigger_pct");
        if (kind == "time" && n.config.value("trigger_time", "").empty())
          errors.push_back("策略单 " + n.id + " 缺少 trigger_time");
      }
    }
  }
  for (const auto& e : p.edges) {
    auto* a = find_node(p, e.from);
    auto* b = find_node(p, e.to);
    if (!a || !b) {
      errors.push_back("连线引用不存在的节点");
      continue;
    }
    // allowed: selector/strategy -> pool, pool -> selector/strategy
    bool ok = false;
    if ((a->type == "selector" || a->type == "strategy") && b->type == "pool") ok = true;
    if (a->type == "pool" && (b->type == "selector" || b->type == "strategy")) ok = true;
    if (!ok) errors.push_back("非法连线: " + a->type + " -> " + b->type);
  }
  if (has_cycle(p)) errors.push_back("工作流存在环，请改为 DAG");
  // orphan functional nodes without pool connection
  for (const auto& n : p.nodes) {
    if (n.type == "pool") continue;
    bool linked = false;
    for (const auto& e : p.edges)
      if (e.from == n.id || e.to == n.id) {
        linked = true;
        break;
      }
    if (!linked) errors.push_back("孤立节点: " + n.id + " (" + n.type + ")");
  }
  return errors;
}

std::vector<std::string> WorkflowEngine::upstream_pools(const WorkflowProgram& p, const std::string& node_id) const {
  std::vector<std::string> out;
  for (const auto& e : p.edges) {
    if (e.to != node_id) continue;
    auto* n = find_node(p, e.from);
    if (n && n->type == "pool") out.push_back(e.from);
  }
  return out;
}

std::vector<std::string> WorkflowEngine::downstream_pools(const WorkflowProgram& p, const std::string& node_id) const {
  std::vector<std::string> out;
  for (const auto& e : p.edges) {
    if (e.from != node_id) continue;
    auto* n = find_node(p, e.to);
    if (n && n->type == "pool") out.push_back(e.to);
  }
  return out;
}

void WorkflowEngine::add_to_pool_locked(WorkflowProgramState& st, const std::string& pool_id, const std::string& code) {
  auto c = upper(grid::normalize_code(code));
  if (c.empty()) return;
  st.pools[pool_id].codes.insert(c);
}

std::vector<std::string> WorkflowEngine::load_watchlist_codes() const {
  std::vector<std::string> out;
  nlohmann::json arr;
  std::string err;
  if (!load_json_array(data_dir_ / "watchlist.json", arr, err) || !arr.is_array()) return out;
  for (const auto& j : arr) {
    auto c = grid::normalize_code(j.value("code", ""));
    if (!c.empty()) out.push_back(upper(c));
  }
  return out;
}

std::string WorkflowEngine::tdx_sign_path() const {
  nlohmann::json obj;
  std::string err;
  if (load_json_object(data_dir_ / "risk.json", obj, err)) return obj.value("tdx_sign_file", "");
  return {};
}

nlohmann::json WorkflowEngine::list_json() {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& p : programs_) arr.push_back(workflow_list_item(p));
  return arr;
}

nlohmann::json WorkflowEngine::get_json(const std::string& id) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto* p = find_locked(id);
  if (!p) return nlohmann::json();
  return workflow_to_json(*p);
}

bool WorkflowEngine::upsert(const nlohmann::json& body, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  WorkflowProgram p = workflow_from_json(body);
  if (p.id.empty()) p.id = new_id();
  p.updated_at = now_str();
  auto errors = validate_program(p);
  if (!errors.empty()) {
    err = errors.front();
    out = {{"errors", errors}};
    return false;
  }
  bool found = false;
  for (auto& x : programs_) {
    if (x.id == p.id) {
      x = p;
      found = true;
      break;
    }
  }
  if (!found) programs_.push_back(p);
  ensure_state_locked(p.id);
  persist_programs_locked();
  persist_state_locked();
  out = workflow_to_json(p);
  return true;
}

bool WorkflowEngine::remove(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto n = programs_.size();
  programs_.erase(std::remove_if(programs_.begin(), programs_.end(), [&](const WorkflowProgram& p) { return p.id == id; }),
                  programs_.end());
  if (programs_.size() == n) {
    err = "not found";
    return false;
  }
  states_.erase(id);
  persist_programs_locked();
  persist_state_locked();
  return true;
}

bool WorkflowEngine::toggle(const std::string& id, nlohmann::json& out, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  auto* p = find_locked(id);
  if (!p) {
    err = "not found";
    return false;
  }
  p->enabled = !p->enabled;
  p->updated_at = now_str();
  persist_programs_locked();
  out = workflow_to_json(*p);
  return true;
}

nlohmann::json WorkflowEngine::validate(const nlohmann::json& body) {
  WorkflowProgram p = workflow_from_json(body);
  auto errors = validate_program(p);
  return {{"ok", errors.empty()}, {"errors", errors}};
}

nlohmann::json WorkflowEngine::state_json(const std::string& id) {
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  ensure_state_locked(id);
  auto* p = find_locked(id);
  if (!p) return nlohmann::json{{"detail", "not found"}};
  auto& st = states_[id];
  nlohmann::json pools = nlohmann::json::object();
  for (const auto& n : p->nodes) {
    if (n.type != "pool") continue;
    nlohmann::json codes = nlohmann::json::array();
    for (const auto& c : st.pools[n.id].codes) codes.push_back(c);
    pools[n.id] = {{"label", n.config.value("label", n.id)}, {"codes", codes}};
  }
  return {{"id", id},
          {"name", p->name},
          {"enabled", p->enabled},
          {"pre_auth", p->pre_auth},
          {"pools", pools},
          {"recent", st.recent}};
}

nlohmann::json WorkflowEngine::template_file_signal(const nlohmann::json& body) {
  auto p = make_file_signal_template(body.value("sign_name", "买入信号"), body.value("trigger_price", 0.0));
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  programs_.push_back(p);
  ensure_state_locked(p.id);
  persist_programs_locked();
  persist_state_locked();
  return workflow_to_json(p);
}

void WorkflowEngine::run_selectors_locked(WorkflowProgram& p, WorkflowProgramState& st, const EventFn& event,
                                          std::string& log_line) {
  for (const auto& n : p.nodes) {
    if (n.type != "selector") continue;
    auto downs = downstream_pools(p, n.id);
    if (downs.empty()) continue;
    auto source = n.config.value("source", "watchlist");
    auto schedule = n.config.value("schedule", "every_poll");
    std::vector<std::string> hits;

    if (source == "watchlist") {
      if (schedule == "once_daily") {
        std::string day = today_ymd();
        if (st.selector_last_run[n.id] == day) continue;
        st.selector_last_run[n.id] = day;
      }
      hits = load_watchlist_codes();
      // optional quote filter on watchlist
      double tp = n.config.value("trigger_price", 0.0);
      double tpct = n.config.value("trigger_pct", 0.0);
      std::string compare = n.config.value("compare", "gte");
      if ((tp > 0 || tpct > 0) && !hits.empty()) {
        std::unordered_map<std::string, grid::Quote> quotes;
        std::string qerr;
        auto src = SettingsStore::instance().load().quote_source;
        grid::fetch_quotes(src.empty() ? "tencent" : src, hits, quotes, qerr);
        std::vector<std::string> filtered;
        for (const auto& code : hits) {
          auto it = quotes.find(code);
          if (it == quotes.end() || it->second.price <= 0) continue;
          double px = it->second.price;
          bool ok = true;
          if (tpct > 0) {
            // treat trigger_pct vs day_anchor in config or skip without anchor — use price only if set
            ok = true;  // pct on watchlist needs ref; use absolute price if set
          }
          if (tp > 0) ok = price_hit(compare, px, tp);
          if (ok) filtered.push_back(code);
        }
        hits = std::move(filtered);
      }
    } else if (source == "file") {
      auto path = tdx_sign_path();
      if (path.empty()) continue;
      std::filesystem::path root = std::filesystem::u8path(path);
      if (!std::filesystem::exists(root)) continue;
      std::string content;
      std::error_code ec;
      if (std::filesystem::is_directory(root, ec)) {
        int nfiles = 0;
        for (auto& ent : std::filesystem::directory_iterator(root, ec)) {
          if (!ent.is_regular_file(ec)) continue;
          std::ifstream in(ent.path());
          if (!in) continue;
          std::stringstream ss;
          ss << in.rdbuf();
          content += ss.str();
          content.push_back('\n');
          if (++nfiles >= 8) break;
        }
      } else {
        std::ifstream in(root);
        if (!in) continue;
        std::stringstream ss;
        ss << in.rdbuf();
        content = ss.str();
      }
      std::string fp = std::to_string(content.size()) + ":" +
                       (content.size() > 100 ? content.substr(content.size() - 100) : content);
      std::string key = p.id + ":" + n.id;
      if (file_fp_[key] == fp) continue;
      file_fp_[key] = fp;
      std::string sign = n.config.value("sign_name", "");
      std::stringstream ls(content);
      std::string line;
      while (std::getline(ls, line)) {
        if (line.empty()) continue;
        if (!sign.empty() && line.find(sign) == std::string::npos) continue;
        auto code = extract_code(line);
        if (!code.empty()) hits.push_back(upper(code));
      }
    } else if (source == "quote") {
      // scan upstream pools or watchlist
      std::vector<std::string> universe;
      auto ups = upstream_pools(p, n.id);
      if (!ups.empty()) {
        for (const auto& pid : ups)
          for (const auto& c : st.pools[pid].codes) universe.push_back(c);
      } else {
        universe = load_watchlist_codes();
      }
      if (universe.empty()) continue;
      if (schedule.rfind("once_daily", 0) == 0 || schedule == "once_daily") {
        auto at = n.config.value("run_at", schedule.size() > 11 ? schedule.substr(11) : "09:25:00");
        if (parse_hms(now_hms()) < parse_hms(at)) continue;
        std::string day = today_ymd();
        if (st.selector_last_run[n.id] == day) continue;
        st.selector_last_run[n.id] = day;
      }
      std::unordered_map<std::string, grid::Quote> quotes;
      std::string qerr;
      auto src = SettingsStore::instance().load().quote_source;
      grid::fetch_quotes(src.empty() ? "tencent" : src, universe, quotes, qerr);
      double tp = n.config.value("trigger_price", 0.0);
      double tpct = n.config.value("trigger_pct", 0.0);
      std::string compare = n.config.value("compare", "gte");
      for (const auto& code : universe) {
        auto it = quotes.find(code);
        if (it == quotes.end() || it->second.price <= 0) continue;
        double px = it->second.price;
        bool ok = false;
        if (tpct > 0) {
          // use open proxy: first price stored in selector_last_run as "day:price" — simplify: skip pct without ref
          // store ref in config day_anchor map via selector_last_run value "YYYYMMDD|price"
          std::string key = st.selector_last_run[n.id + ":ref:" + code];
          double ref = 0;
          if (key.size() > 9 && key.substr(0, 8) == today_ymd()) {
            try {
              ref = std::stod(key.substr(9));
            } catch (...) {
            }
          }
          if (ref <= 0) {
            st.selector_last_run[n.id + ":ref:" + code] = today_ymd() + "|" + std::to_string(px);
            continue;
          }
          double pct = (px - ref) / ref * 100.0;
          ok = (compare == "lte") ? (pct <= -std::abs(tpct)) : (pct >= std::abs(tpct));
        } else if (has_conditions(n.config)) {
          ok = conditions_pass(n.config, quote_env(it->second));
        } else {
          ok = price_hit(compare, px, tp);
        }
        if (ok) hits.push_back(code);
      }
    }

    for (const auto& code : hits) {
      for (const auto& pid : downs) add_to_pool_locked(st, pid, code);
      if (event) event("workflow_select", p.name + " " + n.config.value("label", n.id) + " +" + code);
      if (!log_line.empty()) log_line += " | ";
      log_line += "wf-sel " + code;
    }
  }
}

void WorkflowEngine::run_strategies_locked(WorkflowProgram& p, WorkflowProgramState& st, const EmitFn& emit,
                                           const EventFn& event, std::string& log_line) {
  for (const auto& n : p.nodes) {
    if (n.type != "strategy") continue;
    auto ups = upstream_pools(p, n.id);
    if (ups.empty()) continue;
    std::vector<std::string> codes;
    for (const auto& pid : ups)
      for (const auto& c : st.pools[pid].codes)
        if (std::find(codes.begin(), codes.end(), c) == codes.end()) codes.push_back(c);
    if (codes.empty()) continue;

    std::unordered_map<std::string, grid::Quote> quotes;
    std::string qerr;
    auto src = SettingsStore::instance().load().quote_source;
    grid::fetch_quotes(src.empty() ? "tencent" : src, codes, quotes, qerr);

    std::string side = n.config.value("side", "buy");
    std::string kind = n.config.value("kind", "price");
    std::string compare = n.config.value("compare", "gte");
    double trigger_price = n.config.value("trigger_price", 0.0);
    double trigger_pct = n.config.value("trigger_pct", 0.0);
    std::string trigger_time = n.config.value("trigger_time", "");
    int qty = n.config.value("qty", 100);
    std::string price_type = n.config.value("price_type", "last");
    double limit_price = n.config.value("limit_price", 0.0);
    bool once = n.config.value("once_per_code_day", true);
    auto downs = downstream_pools(p, n.id);
    std::string day = today_ymd();

    for (const auto& code : codes) {
      std::string fire_key = code + ":" + day;
      if (once && st.strategy_fired[n.id].count(fire_key)) continue;

      bool hit = false;
      double px = 0;
      std::string qname;
      auto it = quotes.find(code);
      if (it != quotes.end()) {
        using namespace std::chrono;
        long long sec = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        grid::note_quote_bar(data_dir_, it->second, sec);
      }
      if (has_conditions(n.config) && it != quotes.end() && it->second.price > 0) {
        hit = conditions_pass(n.config, quote_env(it->second));
        qname = it->second.name;
        if (hit) px = limit_price > 0 ? limit_price : quote_price_by_type(it->second, price_type);
      } else if (kind == "time") {
        if (!trigger_time.empty() && parse_hms(now_hms()) >= parse_hms(trigger_time)) {
          hit = true;
          if (it != quotes.end()) {
            px = limit_price > 0 ? limit_price : quote_price_by_type(it->second, price_type);
            qname = it->second.name;
          } else if (limit_price > 0)
            px = limit_price;
        }
      } else if (it != quotes.end() && it->second.price > 0) {
        qname = it->second.name;
        if (kind == "pct") {
          std::string ref_key = n.id + ":anchor:" + code;
          std::string stored = st.selector_last_run[ref_key];
          double ref = 0;
          if (stored.size() > 9 && stored.substr(0, 8) == day) {
            try {
              ref = std::stod(stored.substr(9));
            } catch (...) {
            }
          }
          if (ref <= 0) {
            st.selector_last_run[ref_key] = day + "|" + std::to_string(it->second.price);
            continue;
          }
          double pct = (it->second.price - ref) / ref * 100.0;
          hit = (compare == "lte") ? (pct <= -std::abs(trigger_pct)) : (pct >= std::abs(trigger_pct));
        } else {
          hit = price_hit(compare, it->second.price, trigger_price);
        }
        if (hit) px = limit_price > 0 ? limit_price : quote_price_by_type(it->second, price_type);
      }
      if (!hit || px <= 0) continue;

      auto targets = alias_targets(p, ups, code);
      int lot = qty == 0 ? 0 : floor_lot(qty, targets.front());
      if (qty > 0 && lot <= 0) continue;

      std::string reason_base = "workflow:" + p.name + ":" + n.config.value("label", n.id);
      std::string reason = p.pre_auth ? reason_base : ("confirm_force:" + reason_base);
      std::string err;
      bool traded = qty == 0;
      if (qty > 0 && emit) {
        for (const auto& trade_code : targets) {
          int one = floor_lot(qty, trade_code);
          if (one <= 0) continue;
          std::string one_err;
          bool ok = emit(trade_code, side, one, px, reason, qname, one_err, order_price_type_for(price_type));
          if (!one_err.empty()) err = one_err;
          traded = traded || ok || one_err == "need_confirm";
        }
      }
      // push downstream on success, deliver-only, or pending confirm
      if (traded || err == "need_confirm") {
        st.strategy_fired[n.id].insert(fire_key);
        for (const auto& pid : downs)
          for (const auto& trade_code : targets) add_to_pool_locked(st, pid, trade_code);
        nlohmann::json ev = {{"ts", now_str()},
                             {"node", n.id},
                             {"code", code},
                             {"side", side},
                             {"price", px},
                             {"qty", lot},
                             {"ok", traded},
                             {"err", err}};
        st.recent.push_back(ev);
        while (st.recent.size() > 50) st.recent.erase(st.recent.begin());
        if (event)
          event(traded ? "workflow_trade" : "workflow_confirm", reason_base + " " + side + " " + code);
        if (!log_line.empty()) log_line += " | ";
        log_line += "wf-st " + code + " " + side;
      } else if (!err.empty() && event) {
        event("workflow_block", err + " " + code);
      }
    }
  }
}

void WorkflowEngine::run_program_locked(WorkflowProgram& p, const EmitFn& emit, const EventFn& event,
                                        std::string& log_line) {
  ensure_state_locked(p.id);
  auto& st = states_[p.id];
  run_selectors_locked(p, st, event, log_line);
  run_strategies_locked(p, st, emit, event, log_line);
}

void WorkflowEngine::tick(const EmitFn& emit, const EventFn& event, std::string& log_line) {
  log_line.clear();
  std::lock_guard<std::mutex> lock(mu_);
  reload_locked();
  bool dirty = false;
  for (auto& p : programs_) {
    if (!p.enabled) continue;
    std::string part;
    run_program_locked(p, emit, event, part);
    if (!part.empty()) {
      dirty = true;
      if (!log_line.empty()) log_line += " | ";
      log_line += part;
    } else {
      // still may have updated anchors
      dirty = true;
    }
  }
  if (dirty) persist_state_locked();
  if (log_line.empty()) log_line = "workflow heartbeat programs=" + std::to_string(programs_.size());
}

}  // namespace desk
