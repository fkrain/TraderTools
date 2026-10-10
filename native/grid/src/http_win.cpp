#include "grid/http_win.hpp"

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace grid {

std::wstring utf16_from_utf8(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring ws(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), n);
  return ws;
}

std::string utf8_from_gbk(const std::string& gbk) {
  if (gbk.empty()) return {};
  int n = MultiByteToWideChar(936, 0, gbk.c_str(), (int)gbk.size(), nullptr, 0);
  std::wstring ws(n, L'\0');
  MultiByteToWideChar(936, 0, gbk.c_str(), (int)gbk.size(), ws.data(), n);
  int m = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
  std::string out(m, '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), m, nullptr, nullptr);
  return out;
}

std::string winhttp_get(const std::wstring& host, const std::wstring& path_and_query, std::string& err,
                        const std::vector<std::wstring>& headers, std::uint16_t port) {
  HINTERNET session = WinHttpOpen(L"FollowDesk/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    err = "WinHttpOpen failed";
    return {};
  }
  HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
  if (!conn) {
    WinHttpCloseHandle(session);
    err = "WinHttpConnect failed";
    return {};
  }
  DWORD flags = (port == 443) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req = WinHttpOpenRequest(conn, L"GET", path_and_query.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) {
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    err = "WinHttpOpenRequest failed";
    return {};
  }
  for (const auto& h : headers) {
    WinHttpAddRequestHeaders(req, h.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  }
  BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (ok) ok = WinHttpReceiveResponse(req, nullptr);
  if (!ok) {
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    err = "WinHttp request failed";
    return {};
  }
  std::string body;
  DWORD avail = 0;
  while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
    std::vector<char> buf(avail);
    DWORD read = 0;
    if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) break;
    body.append(buf.data(), read);
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(conn);
  WinHttpCloseHandle(session);
  return body;
}

std::string market_suffix_for_num(const std::string& num) {
  // BJ: 43/82/83/87/88/92；SH: 6*/5*/11*；其余 SZ（含 00/30/12/15/16 LOF·ETF）
  if (num.size() != 6) return ".SZ";
  if (num.compare(0, 2, "92") == 0 || num.compare(0, 2, "83") == 0 || num.compare(0, 2, "87") == 0 ||
      num.compare(0, 2, "43") == 0 || num.compare(0, 2, "82") == 0 || num.compare(0, 2, "88") == 0) {
    return ".BJ";
  }
  if (num[0] == '6' || num[0] == '5' || num.compare(0, 2, "11") == 0) return ".SH";
  return ".SZ";
}

std::string normalize_code(const std::string& raw) {
  std::string code = raw;
  while (!code.empty() && (code.front() == ' ' || code.front() == '\t')) code.erase(code.begin());
  while (!code.empty() && (code.back() == ' ' || code.back() == '\t')) code.pop_back();
  for (auto& c : code) c = (char)toupper((unsigned char)c);
  if (code.empty()) return code;
  if (code.size() >= 8 && (code.compare(0, 2, "SH") == 0 || code.compare(0, 2, "SZ") == 0 ||
                           code.compare(0, 2, "BJ") == 0 || code.compare(0, 2, "HK") == 0)) {
    std::string num = code.substr(2);
    if (num.size() == 6 && num.find_first_not_of("0123456789") == std::string::npos) {
      if (code.compare(0, 2, "HK") == 0) return num + ".HK";
      return num + market_suffix_for_num(num);
    }
    return num + "." + code.substr(0, 2);
  }
  auto dot = code.find('.');
  if (dot != std::string::npos) {
    std::string num = code.substr(0, dot);
    if (num.size() == 6 && num.find_first_not_of("0123456789") == std::string::npos) {
      // 按代码规则重判市场，避免 92xxxx.SH 这类错误后缀
      return num + market_suffix_for_num(num);
    }
    return code;
  }
  if (code.size() == 6 && code.find_first_not_of("0123456789") == std::string::npos) {
    return code + market_suffix_for_num(code);
  }
  return code;
}

std::string to_tencent_symbol(const std::string& code) {
  auto pos = code.find('.');
  std::string num = pos == std::string::npos ? code : code.substr(0, pos);
  std::string mkt = pos == std::string::npos ? "" : code.substr(pos + 1);
  for (auto& c : mkt) c = (char)toupper((unsigned char)c);
  if (mkt == "SH") return "sh" + num;
  if (mkt == "BJ") return "bj" + num;
  return "sz" + num;
}

std::string to_sina_symbol(const std::string& code) {
  return to_tencent_symbol(code);  // same sh/sz prefix
}

std::string to_secid(const std::string& code) {
  auto n = normalize_code(code);
  auto pos = n.find('.');
  std::string num = pos == std::string::npos ? n : n.substr(0, pos);
  std::string mkt = pos == std::string::npos ? "" : n.substr(pos + 1);
  // 东财: 沪市 1.xxxxxx；深市/北交所 0.xxxxxx
  if (mkt == "SH") return "1." + num;
  return "0." + num;
}

}  // namespace grid
