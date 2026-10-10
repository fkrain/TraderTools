#include "desk/paths.hpp"

#include <windows.h>

namespace desk {

std::filesystem::path find_project_root() {
  wchar_t buf[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::filesystem::path p = std::filesystem::path(buf).parent_path();
  for (int i = 0; i < 8; ++i) {
    if (std::filesystem::exists(p / "web" / "index.html")) {
      return std::filesystem::weakly_canonical(p);
    }
    if (!p.has_parent_path() || p == p.root_path()) break;
    p = p.parent_path();
  }
  return std::filesystem::current_path();
}

std::string narrow(const std::filesystem::path& p) {
  const std::wstring ws = p.wstring();
  if (ws.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring ws(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), n);
  return ws;
}

}  // namespace desk
