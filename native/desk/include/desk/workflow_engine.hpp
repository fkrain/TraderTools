#pragma once

#include "desk/workflow.hpp"

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace desk {

class WorkflowEngine {
 public:
  using EmitFn = std::function<bool(const std::string& code, const std::string& side, int qty, double price,
                                    const std::string& reason, const std::string& name, std::string& err,
                                    const std::string& price_type)>;
  using EventFn = std::function<void(const std::string& kind, const std::string& msg)>;

  void init(const std::filesystem::path& data_dir);

  nlohmann::json list_json();
  nlohmann::json get_json(const std::string& id);
  bool upsert(const nlohmann::json& body, nlohmann::json& out, std::string& err);
  bool remove(const std::string& id, std::string& err);
  bool toggle(const std::string& id, nlohmann::json& out, std::string& err);
  nlohmann::json validate(const nlohmann::json& body);
  nlohmann::json state_json(const std::string& id);
  nlohmann::json template_file_signal(const nlohmann::json& body);

  void tick(const EmitFn& emit, const EventFn& event, std::string& log_line);

 private:
  void reload_locked();
  void persist_programs_locked();
  void persist_state_locked();
  void ensure_state_locked(const std::string& program_id);
  WorkflowProgram* find_locked(const std::string& id);
  std::vector<std::string> validate_program(const WorkflowProgram& p) const;
  void run_program_locked(WorkflowProgram& p, const EmitFn& emit, const EventFn& event, std::string& log_line);
  void run_selectors_locked(WorkflowProgram& p, WorkflowProgramState& st, const EventFn& event, std::string& log_line);
  void run_strategies_locked(WorkflowProgram& p, WorkflowProgramState& st, const EmitFn& emit, const EventFn& event,
                             std::string& log_line);
  std::vector<std::string> upstream_pools(const WorkflowProgram& p, const std::string& node_id) const;
  std::vector<std::string> downstream_pools(const WorkflowProgram& p, const std::string& node_id) const;
  void add_to_pool_locked(WorkflowProgramState& st, const std::string& pool_id, const std::string& code);
  std::vector<std::string> load_watchlist_codes() const;
  std::string tdx_sign_path() const;

  std::mutex mu_;
  std::filesystem::path data_dir_;
  std::filesystem::path programs_path_;
  std::filesystem::path state_path_;
  std::vector<WorkflowProgram> programs_;
  std::unordered_map<std::string, WorkflowProgramState> states_;
  std::unordered_map<std::string, std::string> file_fp_;
};

}  // namespace desk
