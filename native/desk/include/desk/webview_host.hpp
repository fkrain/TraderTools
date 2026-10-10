#pragma once

#include <windows.h>

#include <string>

namespace desk {

// Creates the main window, embeds WebView2, navigates to start_url.
// Runs the Win32 message loop until the window closes. Returns exit code.
int run_webview_app(HINSTANCE instance, const std::wstring& start_url, const std::wstring& title);

}  // namespace desk
