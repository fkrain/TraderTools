#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace desk {

nlohmann::json broker_status_json();
nlohmann::json broker_connect_json();

}  // namespace desk
