#include "desk/app_state.hpp"
#include "desk/http_api.hpp"
#include "desk/paths.hpp"
#include "desk/settings.hpp"
#include "desk/webview_host.hpp"

#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <chrono>
#include <thread>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com)) {
    MessageBoxW(nullptr, L"COM init failed", L"FollowDesk", MB_ICONERROR);
    return 1;
  }

  auto root = desk::find_project_root();
  desk::SettingsStore::instance().init(root / "data");
  desk::AppState::instance().init(root);

  desk::HttpServer http;
  std::atomic<bool> http_ready{false};
  std::thread http_thr([&] {
    desk::run_http_server(http, 18787);
  });

  for (int i = 0; i < 100; ++i) {
    if (http.port.load() != 0) {
      http_ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (!http_ready) {
    MessageBoxW(nullptr, L"Local HTTP server failed to start.", L"FollowDesk", MB_ICONERROR);
    desk::request_http_stop();
    if (http_thr.joinable()) http_thr.join();
    CoUninitialize();
    return 1;
  }

  std::wstring url = desk::widen(http.base_url);
  std::wstring title = L"\u8DDF\u5355\u53F0";  // 跟单台
  int code = desk::run_webview_app(instance, url, title);

  desk::AppState::instance().shutdown();
  desk::request_http_stop();
  if (http_thr.joinable()) http_thr.join();
  CoUninitialize();
  return code;
}
