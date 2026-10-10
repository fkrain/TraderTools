#include "grid/order_file.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace grid {
namespace {

std::string now_compact() {
  using clock = std::chrono::system_clock;
  auto t = clock::to_time_t(clock::now());
  std::tm tm{};
  localtime_s(&tm, &t);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y%m%d%H%M%S");
  return oss.str();
}

std::string now_str() {
  using clock = std::chrono::system_clock;
  auto t = clock::to_time_t(clock::now());
  std::tm tm{};
  localtime_s(&tm, &t);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

std::string random_id() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(0, 15);
  const char* hex = "0123456789abcdef";
  std::string s(12, '0');
  for (char& c : s) c = hex[dist(rng)];
  return s;
}

}  // namespace

bool write_order_json(const fs::path& orders_dir, OrderIntent& intent, std::string& err) {
  for (const char* name : {"outbox", "sent", "ack", "done", "reject"}) {
    std::error_code ec;
    fs::create_directories(orders_dir / name, ec);
  }
  if (intent.client_order_id.empty()) intent.client_order_id = random_id();
  std::string cid = intent.client_order_id;
  std::string side = intent.side;
  for (auto& c : side) c = (char)toupper((unsigned char)c);
  std::string code_fn;
  for (char c : intent.code) {
    if (c != '.') code_fn.push_back((char)toupper((unsigned char)c));
  }
  std::string fname = now_compact() + "_" + cid + "_" + side + "_" + code_fn + ".json";
  fs::path path = orders_dir / "outbox" / fname;
  fs::path tmp = path;
  tmp += ".tmp";

  json j = {
      {"v", 1},
      {"client_order_id", cid},
      {"code", intent.code},
      {"side", intent.side},
      {"qty", intent.qty},
      {"price_type", intent.price_type},
      {"price", intent.price},
      {"account_id", intent.account_id},
      {"name", intent.name},
      {"reason", intent.reason},
      {"margin_buy", intent.margin_buy},
      {"created_at", now_str()},
  };
  {
    std::ofstream out(tmp, std::ios::binary);
    if (!out) {
      err = "cannot write outbox";
      return false;
    }
    out << j.dump(2);
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) {
    err = "rename outbox failed: " + ec.message();
    return false;
  }
  return true;
}

}  // namespace grid
