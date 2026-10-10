#include "grid/quote_factory.hpp"

#include "grid/http_win.hpp"
#include "grid/quote_tencent.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <sstream>

namespace grid {

bool fetch_sina_quotes(const std::vector<std::string>& codes, std::unordered_map<std::string, Quote>& out,
                       std::string& err) {
  if (codes.empty()) return true;
  std::ostringstream oss;
  oss << "/list=";
  std::unordered_map<std::string, std::string> mapping;
  for (size_t i = 0; i < codes.size(); ++i) {
    if (i) oss << ",";
    auto sym = to_sina_symbol(codes[i]);
    oss << sym;
    mapping[sym] = normalize_code(codes[i]);
  }
  std::vector<std::wstring> headers = {
      L"Referer: https://finance.sina.com.cn/",
      L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
  };
  std::string body = winhttp_get(L"hq.sinajs.cn", utf16_from_utf8(oss.str()), err, headers);
  if (body.empty()) return false;
  std::string text = utf8_from_gbk(body);
  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    auto a = line.find("hq_str_");
    auto b = line.find("=\"");
    auto c = line.rfind("\"");
    if (a == std::string::npos || b == std::string::npos || c == std::string::npos || c <= b + 2) continue;
    std::string sym = line.substr(a + 7, b - (a + 7));
    std::string payload = line.substr(b + 2, c - (b + 2));
    if (payload.empty()) continue;
    std::vector<std::string> parts;
    std::stringstream ps(payload);
    std::string p;
    while (std::getline(ps, p, ',')) parts.push_back(p);
    if (parts.size() < 4) continue;
    Quote q;
    q.name = parts[0];
    try {
      q.price = std::stod(parts[3]);
    } catch (...) {
      q.price = 0;
    }
    auto to_f = [](const std::string& s) {
      try {
        return std::stod(s);
      } catch (...) {
        return 0.0;
      }
    };
    if (parts.size() > 6) q.bid = to_f(parts[6]);
    if (parts.size() > 7) q.ask = to_f(parts[7]);
    // 11,13,15,17,19 bid1..5 ; 23,25,27,29,31 ask1..5
    for (int i = 0; i < 5; ++i) {
      size_t bi = 11 + static_cast<size_t>(i) * 2;
      size_t ai = 23 + static_cast<size_t>(i) * 2;
      if (parts.size() > bi) q.bids[i] = to_f(parts[bi]);
      if (parts.size() > ai) q.asks[i] = to_f(parts[ai]);
    }
    if (q.bids[0] <= 0) q.bids[0] = q.bid;
    if (q.asks[0] <= 0) q.asks[0] = q.ask;
    if (q.bid <= 0) q.bid = q.bids[0];
    if (q.ask <= 0) q.ask = q.asks[0];
    auto it = mapping.find(sym);
    q.code = it != mapping.end() ? it->second : normalize_code(sym);
    if (q.price > 0) out[q.code] = q;
  }
  return true;
}

bool fetch_eastmoney_quotes(const std::vector<std::string>& codes, std::unordered_map<std::string, Quote>& out,
                            std::string& err) {
  if (codes.empty()) return true;
  std::ostringstream oss;
  oss << "/api/qt/ulist.np/get?fltt=2&fields=f2,f3,f12,f14&ut=b2884a393a59ad64002292a3e90d46a5&secids=";
  std::unordered_map<std::string, std::string> mapping;
  for (size_t i = 0; i < codes.size(); ++i) {
    if (i) oss << ",";
    auto secid = to_secid(codes[i]);
    oss << secid;
    auto pos = secid.find('.');
    std::string num = pos == std::string::npos ? secid : secid.substr(pos + 1);
    mapping[num] = normalize_code(codes[i]);
  }
  std::vector<std::wstring> headers = {
      L"Referer: https://quote.eastmoney.com/",
      L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
  };
  std::string body = winhttp_get(L"push2.eastmoney.com", utf16_from_utf8(oss.str()), err, headers);
  if (body.empty()) return false;
  try {
    auto root = nlohmann::json::parse(body);
    auto diff = root.value("data", nlohmann::json::object()).value("diff", nlohmann::json::array());
    for (const auto& row : diff) {
      std::string num = row.value("f12", "");
      Quote q;
      q.code = mapping.count(num) ? mapping[num] : normalize_code(num);
      q.name = row.value("f14", "");
      if (row.contains("f2") && !row["f2"].is_null() && row["f2"].is_number()) q.price = row["f2"].get<double>();
      else if (row.contains("f2") && row["f2"].is_string()) {
        try {
          q.price = std::stod(row["f2"].get<std::string>());
        } catch (...) {
        }
      }
      if (q.price > 0) out[q.code] = q;
    }
  } catch (const std::exception& ex) {
    err = std::string("eastmoney parse: ") + ex.what();
    return false;
  }
  return true;
}

bool fetch_quotes(const std::string& source, const std::vector<std::string>& codes,
                  std::unordered_map<std::string, Quote>& out, std::string& err) {
  std::vector<std::string> norms;
  norms.reserve(codes.size());
  for (const auto& c : codes) norms.push_back(normalize_code(c));
  std::string key = source;
  for (auto& c : key) c = (char)tolower((unsigned char)c);
  if (key == "sina") return fetch_sina_quotes(norms, out, err);
  if (key == "eastmoney") return fetch_eastmoney_quotes(norms, out, err);
  // default cascade for resilience
  if (fetch_tencent_quotes(norms, out, err) && !out.empty()) return true;
  out.clear();
  std::string e2;
  if (fetch_sina_quotes(norms, out, e2) && !out.empty()) {
    err = "tencent fail; fallback sina";
    return true;
  }
  if (fetch_eastmoney_quotes(norms, out, e2) && !out.empty()) {
    err = "tencent/sina fail; fallback eastmoney";
    return true;
  }
  return false;
}

}  // namespace grid
