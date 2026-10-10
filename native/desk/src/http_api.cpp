#include "desk/http_api.hpp"

#include "desk/app_state.hpp"
#include "desk/broker_status.hpp"
#include "desk/follow.hpp"
#include "desk/portfolios.hpp"
#include "desk/settings.hpp"
#include "grid/quote_factory.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace desk {
namespace {

httplib::Server* g_server = nullptr;
std::mutex g_server_mu;

std::string mime_of(const fs::path& p) {
  auto ext = p.extension().string();
  for (auto& c : ext) c = (char)tolower((unsigned char)c);
  if (ext == ".html") return "text/html; charset=utf-8";
  if (ext == ".js") return "application/javascript; charset=utf-8";
  if (ext == ".css") return "text/css; charset=utf-8";
  if (ext == ".json") return "application/json; charset=utf-8";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".png") return "image/png";
  if (ext == ".ico") return "image/x-icon";
  if (ext == ".woff2") return "font/woff2";
  return "application/octet-stream";
}

bool read_file(const fs::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream oss;
  oss << in.rdbuf();
  out = oss.str();
  return true;
}

void set_cors(httplib::Response& res) {
  res.set_header("Access-Control-Allow-Origin", "*");
  res.set_header("Access-Control-Allow-Headers", "Content-Type");
  res.set_header("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
}

}  // namespace

void request_http_stop() {
  std::lock_guard<std::mutex> lock(g_server_mu);
  if (g_server) g_server->stop();
}

bool run_http_server(HttpServer& state, std::uint16_t preferred_port) {
  httplib::Server svr;
  {
    std::lock_guard<std::mutex> lock(g_server_mu);
    g_server = &svr;
  }

  auto& app = AppState::instance();
  const fs::path web = app.web_dir();

  svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
    set_cors(res);
    res.status = 204;
  });

  auto send_json = [](httplib::Response& res, int status, const std::string& body) {
    set_cors(res);
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
  };

  svr.Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, R"({"ok":true,"host":"cpp-desktop"})");
  });

  svr.Get("/api/settings", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, SettingsStore::instance().public_json().dump());
  });
  svr.Put("/api/settings", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      json patch = req.body.empty() ? json::object() : json::parse(req.body);
      SettingsStore::instance().save_patch(patch);
      send_json(res, 200, SettingsStore::instance().public_json().dump());
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
    }
  });

  svr.Get("/api/quotes", [&](const httplib::Request& req, httplib::Response& res) {
    std::string codes_param = req.get_param_value("codes");
    std::vector<std::string> codes;
    std::stringstream ss(codes_param);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
      while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
      while (!tok.empty() && tok.back() == ' ') tok.pop_back();
      if (!tok.empty()) codes.push_back(tok);
    }
    auto src = SettingsStore::instance().load().quote_source;
    std::unordered_map<std::string, grid::Quote> quotes;
    std::string err;
    if (!grid::fetch_quotes(src, codes, quotes, err)) {
      send_json(res, 502, json{{"detail", err}}.dump());
      return;
    }
    json items = json::array();
    for (auto& kv : quotes) {
      const auto& q = kv.second;
      items.push_back({{"code", q.code},
                       {"name", q.name},
                       {"price", q.price},
                       {"bid", q.bid},
                       {"ask", q.ask},
                       {"bids", {q.bids[0], q.bids[1], q.bids[2], q.bids[3], q.bids[4]}},
                       {"asks", {q.asks[0], q.asks[1], q.asks[2], q.asks[3], q.asks[4]}},
                       {"source", src}});
    }
    send_json(res, 200, json{{"items", items}}.dump());
  });

  svr.Get("/api/follow", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, follow_snapshot_json().dump());
  });
  svr.Post("/api/follow", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      json body = req.body.empty() ? json::object() : json::parse(req.body);
      json out;
      std::string err;
      if (!follow_start(body, out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
      send_json(res, 200, out.dump());
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
    }
  });
  svr.Delete("/api/follow", [&](const httplib::Request&, httplib::Response& res) {
    follow_stop();
    send_json(res, 200, R"({"active":false})");
  });
  svr.Post("/api/follow/sync", [&](const httplib::Request&, httplib::Response& res) {
    json out;
    std::string err;
    if (!follow_sync(out, err)) {
      send_json(res, err.find("paused") != std::string::npos ? 400 : 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Post("/api/follow/pause", [&](const httplib::Request&, httplib::Response& res) {
    json out;
    std::string err;
    if (!follow_pause(out, err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Post("/api/follow/resume", [&](const httplib::Request&, httplib::Response& res) {
    json out;
    std::string err;
    if (!follow_resume(out, err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });

  svr.Get("/api/broker/status", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, broker_status_json().dump());
  });
  svr.Post("/api/broker/connect", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, broker_connect_json().dump());
  });

  svr.Get("/api/portfolios", [&](const httplib::Request& req, httplib::Response& res) {
    std::string source = req.get_param_value("source");
    std::string rank = req.has_param("rank") ? req.get_param_value("rank") : "daily";
    int page = 1, size = 30;
    try {
      if (req.has_param("page")) page = std::stoi(req.get_param_value("page"));
      if (req.has_param("size")) size = std::stoi(req.get_param_value("size"));
    } catch (...) {
    }
    std::string q = req.get_param_value("q");
    int status = 200;
    std::string err;
    auto body = portfolios_list_json(source, rank, page, size, q, status, err);
    send_json(res, status, body);
  });
  svr.Get(R"(/api/portfolios/([^/]+)/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
    int status = 200;
    std::string err;
    auto body = portfolio_detail_json(req.matches[1], req.matches[2], status, err);
    send_json(res, status, body);
  });

  // ---- grid (native) ----
  svr.Get("/api/grid", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, app.grid_list_json());
  });
  svr.Get("/api/grid/engine", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, app.engine_status_json());
  });
  svr.Post("/api/grid/engine/start", [&](const httplib::Request& req, httplib::Response& res) {
    bool dry = req.get_param_value("dry_run") == "true" || req.get_param_value("dry_run") == "1";
    int poll = 3000;
    try {
      if (req.has_param("poll_ms")) poll = std::stoi(req.get_param_value("poll_ms"));
    } catch (...) {
    }
    std::string err;
    if (!app.engine_start(dry, poll, err)) {
      send_json(res, 500, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, app.engine_status_json());
  });
  svr.Post("/api/grid/engine/stop", [&](const httplib::Request&, httplib::Response& res) {
    app.engine_stop();
    send_json(res, 200, app.engine_status_json());
  });
  svr.Post("/api/grid/backtest", [&](const httplib::Request& req, httplib::Response& res) {
    std::string out, err;
    if (!app.grid_backtest(req.body, out, err)) {
      send_json(res, 400, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out);
  });
  svr.Post("/api/grid", [&](const httplib::Request& req, httplib::Response& res) {
    std::string out, err;
    if (!app.grid_create(req.body, out, err)) {
      send_json(res, 400, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out);
  });
  svr.Put(R"(/api/grid/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string out, err;
    if (!app.grid_update(req.matches[1], req.body, out, err)) {
      send_json(res, err == "not found" ? 404 : 400, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out);
  });
  svr.Delete(R"(/api/grid/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!app.grid_delete(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });
  svr.Post(R"(/api/grid/([^/]+)/toggle)", [&](const httplib::Request& req, httplib::Response& res) {
    std::string out, err;
    if (!app.grid_toggle(req.matches[1], out, err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out);
  });

  auto& pe = app.products();
  svr.Get("/api/dashboard", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, pe.dashboard_json().dump());
  });
  svr.Get("/api/alerts", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, pe.list_alerts().dump());
  });
  svr.Post("/api/alerts", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!pe.upsert_alert(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/alerts/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!pe.delete_alert(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });
  svr.Post(R"(/api/alerts/([^/]+)/toggle)", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    if (!pe.toggle_alert(req.matches[1], out, err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });

  svr.Get("/api/stoploss", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, pe.list_stops().dump());
  });
  svr.Post("/api/stoploss", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!pe.upsert_stop(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/stoploss/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!pe.delete_stop(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });
  svr.Post(R"(/api/stoploss/([^/]+)/toggle)", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    if (!pe.toggle_stop(req.matches[1], out, err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });

  svr.Get("/api/conditions", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, pe.list_conds().dump());
  });
  svr.Post("/api/conditions", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!pe.upsert_cond(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/conditions/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!pe.delete_cond(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });
  svr.Post(R"(/api/conditions/([^/]+)/toggle)", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    if (!pe.toggle_cond(req.matches[1], out, err)) {
      send_json(res, err == "not found" ? 404 : 400, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });

  svr.Get("/api/watchlist", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, pe.list_watch().dump());
  });
  svr.Post("/api/watchlist", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!pe.upsert_watch(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/watchlist/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!pe.delete_watch(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });

  svr.Get("/api/risk", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, pe.risk_json().dump());
  });
  svr.Put("/api/risk", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!pe.save_risk(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });

  svr.Get("/api/disclaimer", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, pe.disclaimer_json().dump());
  });
  svr.Post("/api/disclaimer/accept", [&](const httplib::Request&, httplib::Response& res) {
    std::string err;
    if (!pe.accept_disclaimer(err)) {
      send_json(res, 400, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, json{{"ok", true}, {"accepted", true}}.dump());
  });
  svr.Get("/api/confirms", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, pe.list_confirms().dump());
  });
  svr.Post(R"(/api/confirms/([^/]+)/accept)", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    if (!pe.decide_confirm(req.matches[1], true, out, err)) {
      send_json(res, 400, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Post(R"(/api/confirms/([^/]+)/reject)", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    if (!pe.decide_confirm(req.matches[1], false, out, err)) {
      send_json(res, 400, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Get("/api/version", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, pe.version_json().dump());
  });

  auto& wf = app.workflows();
  svr.Get("/api/workflows", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, wf.list_json().dump());
  });
  svr.Post("/api/workflows", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!wf.upsert(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}, {"errors", out.value("errors", json::array())}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Post("/api/workflows/template/file-signal", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      auto body = req.body.empty() ? json::object() : json::parse(req.body);
      send_json(res, 200, wf.template_file_signal(body).dump());
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
    }
  });
  svr.Post("/api/workflows/validate", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      send_json(res, 200, wf.validate(json::parse(req.body)).dump());
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
    }
  });
  svr.Get(R"(/api/workflows/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    auto j = wf.get_json(req.matches[1]);
    if (j.is_null() || j.empty()) {
      send_json(res, 404, json{{"detail", "not found"}}.dump());
      return;
    }
    send_json(res, 200, j.dump());
  });
  svr.Put(R"(/api/workflows/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      auto body = json::parse(req.body);
      body["id"] = std::string(req.matches[1]);
      if (!wf.upsert(body, out, err)) {
        send_json(res, 400, json{{"detail", err}, {"errors", out.value("errors", json::array())}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/workflows/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!wf.remove(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, json{{"ok", true}}.dump());
  });
  svr.Post(R"(/api/workflows/([^/]+)/toggle)", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    if (!wf.toggle(req.matches[1], out, err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Get(R"(/api/workflows/([^/]+)/state)", [&](const httplib::Request& req, httplib::Response& res) {
    auto j = wf.state_json(req.matches[1]);
    if (j.contains("detail")) {
      send_json(res, 404, j.dump());
      return;
    }
    send_json(res, 200, j.dump());
  });

  svr.Get("/api/orders", [&](const httplib::Request& req, httplib::Response& res) {
    int limit = 80;
    try {
      if (req.has_param("limit")) limit = std::stoi(req.get_param_value("limit"));
    } catch (...) {
    }
    send_json(res, 200, pe.orders_json(limit).dump());
  });
  svr.Get("/api/events", [&](const httplib::Request& req, httplib::Response& res) {
    int limit = 80;
    try {
      if (req.has_param("limit")) limit = std::stoi(req.get_param_value("limit"));
    } catch (...) {
    }
    send_json(res, 200, pe.events_json(limit).dump());
  });
  svr.Post("/api/flash", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!pe.flash_order(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });

  auto& mod = app.modules();
  svr.Get("/api/t0", [&](const httplib::Request&, httplib::Response& res) { send_json(res, 200, mod.list_t0().dump()); });
  svr.Post("/api/t0", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!mod.upsert_t0(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/t0/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!mod.delete_t0(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });
  svr.Post(R"(/api/t0/([^/]+)/toggle)", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    if (!mod.toggle_t0(req.matches[1], out, err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });

  svr.Get("/api/baskets", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, mod.list_baskets().dump());
  });
  svr.Post("/api/baskets", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!mod.upsert_basket(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/baskets/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!mod.delete_basket(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });
  svr.Post(R"(/api/baskets/([^/]+)/run)", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    if (!mod.run_basket(req.matches[1], out, err)) {
      send_json(res, 400, json{{"detail", err}}.dump());
      return;
    }
    if (out.contains("legs")) {
      for (auto& leg : out["legs"]) {
        if (!leg.value("ok", false)) continue;
        std::string e2;
        bool ok = pe.place_order(leg.value("code", ""), leg.value("side", "buy"), leg.value("qty", 0),
                                 leg.value("price", 0.0), "basket", "", e2);
        leg["submitted"] = ok;
        if (!ok) leg["err"] = e2;
      }
    }
    send_json(res, 200, out.dump());
  });

  svr.Get("/api/board", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, mod.list_boards().dump());
  });
  svr.Post("/api/board", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!mod.upsert_board(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/board/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!mod.delete_board(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });
  svr.Post(R"(/api/board/([^/]+)/toggle)", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    if (!mod.toggle_board(req.matches[1], out, err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });

  svr.Get("/api/formulas", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, mod.list_formulas().dump());
  });
  svr.Post("/api/formulas", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!mod.upsert_formula(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/formulas/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!mod.delete_formula(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });

  svr.Get("/api/accounts", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, mod.list_accounts().dump());
  });
  svr.Post("/api/accounts", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!mod.upsert_account(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/accounts/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!mod.delete_account(req.matches[1], err)) {
      send_json(res, 400, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });

  svr.Get("/api/curve", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, mod.curve_json().dump());
  });
  svr.Get("/api/templates", [&](const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, mod.list_templates().dump());
  });
  svr.Post("/api/templates", [&](const httplib::Request& req, httplib::Response& res) {
    json out;
    std::string err;
    try {
      if (!mod.save_template(json::parse(req.body), out, err)) {
        send_json(res, 400, json{{"detail", err}}.dump());
        return;
      }
    } catch (const std::exception& ex) {
      send_json(res, 400, json{{"detail", ex.what()}}.dump());
      return;
    }
    send_json(res, 200, out.dump());
  });
  svr.Delete(R"(/api/templates/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    if (!mod.delete_template(req.matches[1], err)) {
      send_json(res, 404, json{{"detail", err}}.dump());
      return;
    }
    send_json(res, 200, R"({"ok":true})");
  });

  // static web
  svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
    set_cors(res);
    std::string body;
    if (!read_file(web / "index.html", body)) {
      res.status = 404;
      res.set_content("index.html missing", "text/plain");
      return;
    }
    res.set_content(body, "text/html; charset=utf-8");
  });
  svr.Get(R"(/static/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
    set_cors(res);
    fs::path rel = req.matches[1].str();
    fs::path full = web / rel;
    std::string body;
    if (!read_file(full, body)) {
      res.status = 404;
      res.set_content("not found", "text/plain");
      return;
    }
    res.set_content(body, mime_of(full));
  });

  state.running = true;
  std::uint16_t port = preferred_port ? preferred_port : 18787;
  bool ok = false;
  for (int i = 0; i < 20; ++i) {
    if (svr.bind_to_port("127.0.0.1", port)) {
      ok = true;
      break;
    }
    ++port;
  }
  if (!ok) {
    state.running = false;
    std::lock_guard<std::mutex> lock(g_server_mu);
    g_server = nullptr;
    return false;
  }
  state.port = port;
  state.base_url = "http://127.0.0.1:" + std::to_string(port) + "/";
  app.append_log("[http] listen " + state.base_url);
  svr.listen_after_bind();
  state.running = false;
  {
    std::lock_guard<std::mutex> lock(g_server_mu);
    g_server = nullptr;
  }
  return true;
}

}  // namespace desk
