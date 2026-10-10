#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace desk {

struct HttpServer {
  std::atomic<bool> running{false};
  std::atomic<std::uint16_t> port{0};
  std::string base_url;
};

// Starts listening on 127.0.0.1, picks a free port if preferred==0.
// Blocks until stop() or failure; call from a worker thread.
bool run_http_server(HttpServer& state, std::uint16_t preferred_port = 18787);
void request_http_stop();

}  // namespace desk
