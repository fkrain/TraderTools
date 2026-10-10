#pragma once

#include "desk/product.hpp"
#include "grid/types.hpp"

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace desk {

// Extra commercial modules: T+0, basket, board, formula, accounts, equity curve, templates
class DeskModules {
 public:
  void init(const std::filesystem::path& data_dir, const std::filesystem::path& orders_dir);

  nlohmann::json list_t0();
  bool upsert_t0(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool delete_t0(const std::string& id, std::string& err);
  bool toggle_t0(const std::string& id, nlohmann::json& out, std::string& err);

  nlohmann::json list_baskets();
  bool upsert_basket(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool delete_basket(const std::string& id, std::string& err);
  bool run_basket(const std::string& id, nlohmann::json& out, std::string& err);

  nlohmann::json list_boards();
  bool upsert_board(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool delete_board(const std::string& id, std::string& err);
  bool toggle_board(const std::string& id, nlohmann::json& out, std::string& err);

  nlohmann::json list_formulas();
  bool upsert_formula(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool delete_formula(const std::string& id, std::string& err);

  nlohmann::json list_accounts();
  bool upsert_account(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool delete_account(const std::string& id, std::string& err);

  nlohmann::json curve_json();
  nlohmann::json list_templates();
  bool save_template(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool delete_template(const std::string& id, std::string& err);

  void tick(std::function<bool(const std::string& code, const std::string& side, int qty, double price,
                               const std::string& reason, const std::string& name, std::string& err,
                               const std::string& price_type)>
                emit,
            const std::function<void(const std::string& kind, const std::string& msg)>& event,
            std::string& log_line);

 private:
  void reload_locked();
  void persist_all_locked();

  std::mutex mu_;
  std::filesystem::path data_dir_;
  std::filesystem::path orders_dir_;
  std::vector<T0Rule> t0_;
  std::vector<Basket> baskets_;
  std::vector<BoardRule> boards_;
  std::vector<FormulaJob> formulas_;
  std::vector<Account> accounts_;
  std::vector<CurvePoint> curve_;
  nlohmann::json templates_ = nlohmann::json::array();
  std::string curve_day_;
  std::string last_sign_fp_;
};

}  // namespace desk
