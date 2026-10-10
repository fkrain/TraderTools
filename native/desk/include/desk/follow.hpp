#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace desk {

nlohmann::json follow_snapshot_json();  // {active, account}
bool follow_start(const nlohmann::json& body, nlohmann::json& out, std::string& err);
bool follow_sync(nlohmann::json& out, std::string& err);
bool follow_pause(nlohmann::json& out, std::string& err);
bool follow_resume(nlohmann::json& out, std::string& err);
void follow_stop();

}  // namespace desk
