#include "desk/webview_host.hpp"

#include <wrl.h>

#include <WebView2.h>

#include <string>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace desk {
namespace {

constexpr wchar_t kWndClass[] = L"FollowDeskMainWnd";

struct HostState {
  HWND hwnd = nullptr;
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2> webview;
  std::wstring start_url;
};

HostState* g_host = nullptr;

void resize_webview(HostState* host) {
  if (!host || !host->controller || !host->hwnd) return;
  RECT rc{};
  GetClientRect(host->hwnd, &rc);
  host->controller->put_Bounds(rc);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_SIZE:
      if (g_host) resize_webview(g_host);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}

bool create_webview(HostState* host) {
  wchar_t local[MAX_PATH] = {};
  GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
  std::wstring user_data = std::wstring(local) + L"\\FollowDesk\\WebView2";
  CreateDirectoryW((std::wstring(local) + L"\\FollowDesk").c_str(), nullptr);
  CreateDirectoryW(user_data.c_str(), nullptr);

  HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, user_data.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [host](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) {
              MessageBoxW(host->hwnd, L"Create WebView2 environment failed.\nInstall Edge WebView2 Runtime.",
                          L"FollowDesk", MB_ICONERROR);
              PostQuitMessage(1);
              return result;
            }
            return env->CreateCoreWebView2Controller(
                host->hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [host](HRESULT result2, ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(result2) || !controller) {
                        MessageBoxW(host->hwnd, L"Create WebView2 controller failed.", L"FollowDesk",
                                    MB_ICONERROR);
                        PostQuitMessage(1);
                        return result2;
                      }
                      host->controller = controller;
                      host->controller->get_CoreWebView2(&host->webview);
                      resize_webview(host);
                      if (host->webview) {
                        host->webview->Navigate(host->start_url.c_str());
                      }
                      return S_OK;
                    })
                    .Get());
          })
          .Get());

  return SUCCEEDED(hr);
}

}  // namespace

int run_webview_app(HINSTANCE instance, const std::wstring& start_url, const std::wstring& title) {
  HostState host;
  host.start_url = start_url;
  g_host = &host;

  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.lpszClassName = kWndClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  RegisterClassW(&wc);

  host.hwnd = CreateWindowExW(0, kWndClass, title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                              CW_USEDEFAULT, 1280, 840, nullptr, nullptr, instance, nullptr);
  if (!host.hwnd) return 1;

  if (!create_webview(&host)) {
    MessageBoxW(nullptr, L"WebView2 bootstrap failed.", L"FollowDesk", MB_ICONERROR);
    return 1;
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (host.controller) {
    host.controller->Close();
    host.controller = nullptr;
  }
  host.webview = nullptr;
  g_host = nullptr;
  return (int)msg.wParam;
}

}  // namespace desk
