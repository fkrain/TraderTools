#include "grid/grid_engine.hpp"

#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static std::atomic<bool> g_running{true};

static void on_signal(int) { g_running = false; }

static void usage() {
  std::cout
      << "grid_engine --data <data_dir> [--poll-ms 3000] [--dry-run]\n"
      << "  Load data/grid.json, poll Tencent quotes, write orders to outbox.\n";
}

int main(int argc, char** argv) {
  fs::path data_dir;
  int poll_ms = 3000;
  bool dry_run = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--data" && i + 1 < argc) {
      data_dir = fs::u8path(argv[++i]);
    } else if (a == "--poll-ms" && i + 1 < argc) {
      poll_ms = std::atoi(argv[++i]);
    } else if (a == "--dry-run") {
      dry_run = true;
    } else if (a == "-h" || a == "--help") {
      usage();
      return 0;
    }
  }

  if (data_dir.empty()) {
    fs::path exe = fs::path(argv[0]).parent_path();
    data_dir = fs::weakly_canonical(exe / ".." / ".." / ".." / "data");
  }

  grid::EngineConfig cfg;
  cfg.data_dir = data_dir;
  cfg.grid_json = data_dir / "grid.json";
  cfg.orders_dir = data_dir / "orders";
  cfg.poll_ms = poll_ms > 500 ? poll_ms : 3000;
  cfg.dry_run = dry_run;
  cfg.write_orders = !dry_run;

  std::cout << "data=" << cfg.data_dir.u8string() << "\n"
            << "grid=" << cfg.grid_json.u8string() << "\n"
            << "orders=" << cfg.orders_dir.u8string() << "\n"
            << "poll_ms=" << cfg.poll_ms << " dry_run=" << (dry_run ? 1 : 0) << "\n";

  grid::GridEngine engine(cfg);
  std::string err;
  if (!engine.load(err)) {
    std::cerr << "load failed: " << err << "\n";
    return 1;
  }
  std::cout << "strategies=" << engine.strategies().size() << "\n";

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  std::string line;
  engine.tick(line);
  std::cout << "first tick: " << line << "\n";

  engine.run_loop(g_running);
  std::cout << "stopped\n";
  return 0;
}
