#include "desk/broker_status.hpp"

#include "desk/settings.hpp"

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace fs = std::filesystem;

namespace desk {
namespace {

bool tcp_open(const std::string& host, int port, int timeout_ms = 400) {
#ifdef _WIN32
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    WSACleanup();
    return false;
  }
  u_long nonblock = 1;
  ioctlsocket(s, FIONBIO, &nonblock);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port));
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    // try localhost
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  }
  int r = connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  bool ok = false;
  if (r == 0) {
    ok = true;
  } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ok = select(0, nullptr, &wfds, nullptr, &tv) > 0;
  }
  closesocket(s);
  WSACleanup();
  return ok;
#else
  (void)host;
  (void)port;
  (void)timeout_ms;
  return false;
#endif
}

}  // namespace

nlohmann::json broker_status_json() {
  auto s = SettingsStore::instance().load();
  const std::string ch = s.broker_channel.empty() ? "paper" : s.broker_channel;

  if (ch == "paper") {
    return {
        {"channel", "paper"},
        {"connected", true},
        {"label", "仅模拟账户"},
        {"detail", "paper · no live orders"},
    };
  }
  if (ch == "easyths" || ch == "mouse_sim" || ch == "file_scan") {
    const std::string host = s.easyths_host.empty() ? "127.0.0.1" : s.easyths_host;
    const int port = s.easyths_port > 0 ? s.easyths_port : 7648;
    bool up = tcp_open(host, port);
    auto root = SettingsStore::instance().orders_root();
    int pending = 0;
    std::error_code ec;
    if (fs::exists(root / "outbox", ec)) {
      for (auto& p : fs::directory_iterator(root / "outbox", ec)) {
        if (p.path().extension() == ".json") ++pending;
      }
    }
    return {
        {"channel", "mouse_sim"},
        {"connected", up},
        {"label", "模拟鼠键"},
        {"detail", host + ":" + std::to_string(port) + (up ? " · up" : " · down") + " · outbox " +
                       std::to_string(pending) + " · bridge: scripts/start_easyths_bridge.ps1"},
        {"easyths_host", host},
        {"easyths_port", port},
    };
  }
  return {
      {"channel", ch},
      {"connected", false},
      {"label", ch},
      {"detail", "unknown channel"},
  };
}

nlohmann::json broker_connect_json() {
  auto s = SettingsStore::instance().load();
  if (s.broker_channel == "file_scan" || s.broker_channel == "easyths" || s.broker_channel == "mouse_sim") {
    SettingsStore::instance().orders_root();
  }
  return broker_status_json();
}

}  // namespace desk
