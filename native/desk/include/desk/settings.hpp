#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <mutex>
#include <string>

namespace desk {

struct Settings {
  std::string xueqiu_cookie;
  std::string quote_source = "tencent";
  std::string broker_channel = "paper";
  std::string orders_dir;
  std::string mouse_window_title;
  // EasyTHS (Tonghuashun GUI automation via local REST)
  std::string easyths_host = "127.0.0.1";
  int easyths_port = 7648;
  std::string easyths_api_key;
  std::string easyths_account;
};

class SettingsStore {
 public:
  static SettingsStore& instance();

  void init(const std::filesystem::path& data_dir);
  Settings load();
  Settings save_patch(const nlohmann::json& patch);
  nlohmann::json public_json();
  nlohmann::json quote_sources_json();
  nlohmann::json broker_channels_json();
  std::filesystem::path orders_root();

 private:
  SettingsStore() = default;
  Settings defaults() const;
  Settings from_json(const nlohmann::json& j) const;
  nlohmann::json to_json(const Settings& s) const;
  std::string mask_cookie(const std::string& cookie) const;

  std::mutex mu_;
  std::filesystem::path path_;
  std::filesystem::path data_dir_;
};

}  // namespace desk
