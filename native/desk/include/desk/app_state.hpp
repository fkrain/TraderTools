#pragma once

#include "desk/desk_modules.hpp"
#include "desk/product_engine.hpp"
#include "desk/workflow_engine.hpp"
#include "grid/grid_engine.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace desk {

class AppState {
 public:
  static AppState& instance();

  void init(const std::filesystem::path& root_dir);
  void shutdown();
  const std::filesystem::path& root() const { return root_; }
  const std::filesystem::path& data_dir() const { return data_dir_; }
  const std::filesystem::path& web_dir() const { return web_dir_; }
  std::string data_dir_utf8() const { return data_dir_.u8string(); }
  std::string web_dir_utf8() const { return web_dir_.u8string(); }

  std::string grid_list_json();
  bool grid_create(const std::string& body_json, std::string& out_json, std::string& err);
  bool grid_update(const std::string& id, const std::string& body_json, std::string& out_json, std::string& err);
  bool grid_delete(const std::string& id, std::string& err);
  bool grid_toggle(const std::string& id, std::string& out_json, std::string& err);
  bool grid_backtest(const std::string& body_json, std::string& out_json, std::string& err);

  std::string engine_status_json();
  bool engine_start(bool dry_run, int poll_ms, std::string& err);
  void engine_stop();

  ProductEngine& products() { return products_; }
  DeskModules& modules() { return modules_; }
  WorkflowEngine& workflows() { return workflows_; }

  void append_log(const std::string& line);
  std::vector<std::string> log_tail(size_t n = 40);

 private:
  AppState() = default;
  void engine_loop();
  void product_loop();

  std::filesystem::path root_;
  std::filesystem::path data_dir_;
  std::filesystem::path web_dir_;
  std::filesystem::path grid_json_;
  std::filesystem::path orders_dir_;

  std::mutex mu_;
  std::unique_ptr<grid::GridEngine> engine_;
  std::atomic<bool> running_{false};
  std::thread thr_;
  bool dry_run_ = false;
  int poll_ms_ = 3000;
  double started_at_ = 0;
  std::vector<std::string> logs_;

  ProductEngine products_;
  DeskModules modules_;
  WorkflowEngine workflows_;
  std::atomic<bool> product_running_{false};
  std::thread product_thr_;
};

}  // namespace desk
