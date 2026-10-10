#pragma once

#include "desk/product.hpp"
#include "grid/types.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace desk {

class ProductEngine {
 public:
  void init(const std::filesystem::path& data_dir, const std::filesystem::path& orders_dir);

  // CRUD
  nlohmann::json list_alerts();
  bool upsert_alert(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool delete_alert(const std::string& id, std::string& err);
  bool toggle_alert(const std::string& id, nlohmann::json& out, std::string& err);

  nlohmann::json list_stops();
  bool upsert_stop(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool delete_stop(const std::string& id, std::string& err);
  bool toggle_stop(const std::string& id, nlohmann::json& out, std::string& err);

  nlohmann::json list_conds();
  bool upsert_cond(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool delete_cond(const std::string& id, std::string& err);
  bool toggle_cond(const std::string& id, nlohmann::json& out, std::string& err);

  nlohmann::json list_watch();
  bool upsert_watch(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool delete_watch(const std::string& id, std::string& err);

  nlohmann::json risk_json();
  bool save_risk(const nlohmann::json& body, nlohmann::json& out, std::string& err);

  nlohmann::json orders_json(int limit = 80);
  nlohmann::json events_json(int limit = 80);
  nlohmann::json dashboard_json();

  nlohmann::json disclaimer_json();
  bool accept_disclaimer(std::string& err);
  nlohmann::json list_confirms();
  bool decide_confirm(const std::string& id, bool accept, nlohmann::json& out, std::string& err);
  nlohmann::json version_json();

  bool flash_order(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool place_order(const std::string& code, const std::string& side, int qty, double price,
                   const std::string& reason, const std::string& name, std::string& err,
                   const std::string& price_type = "limit");
  void note_event(const std::string& kind, const std::string& msg);

  // one poll cycle
  void tick(std::string& log_line);

 private:
  void reload_locked();
  void persist_alerts_locked();
  void persist_stops_locked();
  void persist_conds_locked();
  void persist_watch_locked();
  void persist_risk_locked();
  void persist_confirms_locked();
  void append_event(const std::string& kind, const std::string& msg, const nlohmann::json& extra = {});
  bool emit_trade(const std::string& code, const std::string& side, int qty, double price,
                  const std::string& reason, const std::string& name, std::string& err,
                  const std::string& price_type = "limit", bool skip_confirm = false);
  bool risk_allows_order(const std::string& code, int qty, double price, std::string& why);
  void bump_order_count(const std::string& code);
  void scan_file_signals(std::string& log_line);
  void eval_price_alerts(const std::unordered_map<std::string, grid::Quote>& quotes, std::string& log_line);
  void eval_stops(const std::unordered_map<std::string, grid::Quote>& quotes, std::string& log_line);
  void eval_conds(const std::unordered_map<std::string, grid::Quote>& quotes, std::string& log_line);
  void eval_watch_bands(const std::unordered_map<std::string, grid::Quote>& quotes, std::string& log_line);
  void refresh_day_equity_locked();

  std::mutex mu_;
  std::filesystem::path data_dir_;
  std::filesystem::path orders_dir_;
  std::filesystem::path alerts_path_;
  std::filesystem::path stops_path_;
  std::filesystem::path conds_path_;
  std::filesystem::path watch_path_;
  std::filesystem::path risk_path_;
  std::filesystem::path risk_state_path_;
  std::filesystem::path events_path_;
  std::filesystem::path confirms_path_;
  std::filesystem::path disclaimer_path_;

  std::vector<AlertRule> alerts_;
  std::vector<StopRule> stops_;
  std::vector<CondOrder> conds_;
  std::vector<WatchItem> watch_;
  std::vector<PendingConfirm> confirms_;
  RiskConfig risk_;
  RiskState risk_state_;
  std::unordered_map<std::string, int64_t> code_last_order_ms_;
  std::string last_sign_fingerprint_;
  bool sound_armed_ = false;
  bool disclaimer_accepted_ = false;
};

}  // namespace desk
