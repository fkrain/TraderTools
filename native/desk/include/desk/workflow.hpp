#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace desk {

struct WorkflowEdge {
  std::string from;
  std::string to;
};

struct WorkflowNode {
  std::string id;
  std::string type;  // selector | pool | strategy
  double x = 80;
  double y = 80;
  nlohmann::json config = nlohmann::json::object();
};

struct WorkflowProgram {
  std::string id;
  std::string name;
  bool enabled = false;
  bool pre_auth = false;
  std::vector<WorkflowNode> nodes;
  std::vector<WorkflowEdge> edges;
  std::string updated_at;
  std::string note;
};

struct WorkflowPoolState {
  std::unordered_set<std::string> codes;
};

struct WorkflowProgramState {
  std::unordered_map<std::string, WorkflowPoolState> pools;  // node_id -> codes
  std::unordered_map<std::string, std::string> selector_last_run;  // node_id -> YYYYMMDD or fingerprint
  std::unordered_map<std::string, std::unordered_set<std::string>> strategy_fired;  // node_id -> code+day
  std::vector<nlohmann::json> recent;  // recent triggers
};

nlohmann::json workflow_to_json(const WorkflowProgram& p);
WorkflowProgram workflow_from_json(const nlohmann::json& j);
nlohmann::json workflow_list_item(const WorkflowProgram& p);

WorkflowProgram make_file_signal_template(const std::string& sign_name, double trigger_price);

}  // namespace desk
