#include "desk/settings.hpp"

#include <cctype>
#include <fstream>

namespace desk {
namespace {

std::string u8(const std::filesystem::path& p) { return p.u8string(); }

}  // namespace

SettingsStore& SettingsStore::instance() {
  static SettingsStore s;
  return s;
}

void SettingsStore::init(const std::filesystem::path& data_dir) {
  std::lock_guard<std::mutex> lock(mu_);
  data_dir_ = data_dir;
  path_ = data_dir / "settings.json";
  std::filesystem::create_directories(data_dir_);
}

std::string normalize_quote_source(std::string key) {
  for (auto& c : key) c = (char)tolower((unsigned char)c);
  if (key == "sina" || key == "eastmoney" || key == "tencent") return key;
  return "tencent";
}

std::string normalize_broker_channel(std::string key) {
  for (auto& c : key) c = (char)tolower((unsigned char)c);
  if (key == "mouse_sim" || key == "easyths" || key == "file_scan") return "mouse_sim";
  return "paper";
}

Settings SettingsStore::defaults() const {
  Settings s;
  s.orders_dir = u8(data_dir_ / "orders");
  return s;
}

Settings SettingsStore::from_json(const nlohmann::json& j) const {
  Settings s = defaults();
  if (!j.is_object()) return s;
  s.xueqiu_cookie = j.value("xueqiu_cookie", s.xueqiu_cookie);
  s.quote_source = normalize_quote_source(j.value("quote_source", s.quote_source));
  s.broker_channel = normalize_broker_channel(j.value("broker_channel", s.broker_channel));
  s.orders_dir = j.value("orders_dir", s.orders_dir);
  s.mouse_window_title = j.value("mouse_window_title", s.mouse_window_title);
  s.easyths_host = j.value("easyths_host", s.easyths_host);
  s.easyths_port = j.value("easyths_port", s.easyths_port);
  s.easyths_api_key = j.value("easyths_api_key", s.easyths_api_key);
  s.easyths_account = j.value("easyths_account", s.easyths_account);
  if (s.orders_dir.empty()) s.orders_dir = u8(data_dir_ / "orders");
  return s;
}

nlohmann::json SettingsStore::to_json(const Settings& s) const {
  return nlohmann::json{
      {"xueqiu_cookie", s.xueqiu_cookie},
      {"quote_source", s.quote_source},
      {"broker_channel", s.broker_channel},
      {"orders_dir", s.orders_dir},
      {"mouse_window_title", s.mouse_window_title},
      {"easyths_host", s.easyths_host},
      {"easyths_port", s.easyths_port},
      {"easyths_api_key", s.easyths_api_key},
      {"easyths_account", s.easyths_account},
  };
}

std::string SettingsStore::mask_cookie(const std::string& cookie) const {
  std::string text = cookie;
  while (!text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\r')) text.erase(text.begin());
  while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r')) text.pop_back();
  if (text.empty()) return "";
  if (text.size() <= 12) return std::string(text.size(), '*');
  return text.substr(0, 6) + "..." + text.substr(text.size() - 4) + " (" + std::to_string(text.size()) + " chars)";
}

Settings SettingsStore::load() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!std::filesystem::exists(path_)) return defaults();
  try {
    std::ifstream in(path_);
    nlohmann::json j;
    in >> j;
    return from_json(j);
  } catch (...) {
    return defaults();
  }
}

Settings SettingsStore::save_patch(const nlohmann::json& patch) {
  std::lock_guard<std::mutex> lock(mu_);
  nlohmann::json cur = to_json(defaults());
  if (std::filesystem::exists(path_)) {
    try {
      std::ifstream in(path_);
      nlohmann::json j;
      in >> j;
      if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) cur[it.key()] = it.value();
      }
    } catch (...) {
    }
  }
  if (patch.is_object()) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
      if (it.value().is_null()) continue;
      // empty cookie / password means keep existing
      if ((it.key() == "xueqiu_cookie" || it.key() == "easyths_api_key") &&
          it.value().is_string() && it.value().get<std::string>().empty()) {
        continue;
      }
      cur[it.key()] = it.value();
    }
  }
  Settings s = from_json(cur);
  auto tmp = path_;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << to_json(s).dump(2);
  }
  std::error_code ec;
  std::filesystem::remove(path_, ec);
  std::filesystem::rename(tmp, path_, ec);
  return s;
}

nlohmann::json SettingsStore::quote_sources_json() {
  return nlohmann::json::array({
      {{"id", "tencent"}, {"label", "腾讯"}},
      {{"id", "sina"}, {"label", "新浪"}},
      {{"id", "eastmoney"}, {"label", "东财"}},
  });
}

nlohmann::json SettingsStore::broker_channels_json() {
  return nlohmann::json::array({
      {{"id", "paper"}, {"label", "仅模拟账户"}},
      {{"id", "mouse_sim"}, {"label", "模拟鼠键"}},
  });
}

nlohmann::json SettingsStore::public_json() {
  Settings s = load();
  nlohmann::json j = {
      {"xueqiu_cookie_set", !s.xueqiu_cookie.empty()},
      {"xueqiu_cookie_preview", mask_cookie(s.xueqiu_cookie)},
      {"quote_source", s.quote_source},
      {"broker_channel", s.broker_channel},
      {"orders_dir", s.orders_dir},
      {"mouse_window_title", s.mouse_window_title},
      {"easyths_host", s.easyths_host},
      {"easyths_port", s.easyths_port},
      {"easyths_api_key_set", !s.easyths_api_key.empty()},
      {"easyths_account", s.easyths_account},
      {"quote_sources", quote_sources_json()},
      {"broker_channels", broker_channels_json()},
      {"host", "cpp-desktop"},
  };
  return j;
}

std::filesystem::path SettingsStore::orders_root() {
  Settings s = load();
  std::filesystem::path root = std::filesystem::u8path(s.orders_dir);
  for (const char* name : {"outbox", "sent", "ack", "done", "reject"}) {
    std::error_code ec;
    std::filesystem::create_directories(root / name, ec);
  }
  return root;
}

}  // namespace desk
