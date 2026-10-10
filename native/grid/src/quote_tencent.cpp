#include "grid/quote_tencent.hpp"
#include "grid/http_win.hpp"

#include <sstream>
#include <unordered_map>
#include <vector>

namespace grid {
namespace {

double to_f(const std::string& s) {
  try {
    return std::stod(s);
  } catch (...) {
    return 0;
  }
}

}  // namespace

bool fetch_tencent_quotes(const std::vector<std::string>& codes, std::unordered_map<std::string, Quote>& out,
                          std::string& err) {
  if (codes.empty()) return true;
  std::ostringstream oss;
  oss << "/q=";
  for (size_t i = 0; i < codes.size(); ++i) {
    if (i) oss << ",";
    oss << to_tencent_symbol(codes[i]);
  }
  std::string body = winhttp_get(L"qt.gtimg.cn", utf16_from_utf8(oss.str()), err);
  if (body.empty()) return false;

  std::unordered_map<std::string, std::string> mapping;
  for (const auto& c : codes) {
    mapping[to_tencent_symbol(c)] = normalize_code(c);
  }

  std::stringstream ss(body);
  std::string chunk;
  while (std::getline(ss, chunk, ';')) {
    auto eq = chunk.find('=');
    if (eq == std::string::npos) continue;
    std::string left = chunk.substr(0, eq);
    std::string right = chunk.substr(eq + 1);
    while (!right.empty() && (right.front() == '"' || right.front() == ' ')) right.erase(right.begin());
    while (!right.empty() && (right.back() == '"' || right.back() == '\r' || right.back() == ' '))
      right.pop_back();
    auto us = left.find_last_of('_');
    std::string sym = us == std::string::npos ? left : left.substr(us + 1);
    for (auto& c : sym) c = (char)tolower((unsigned char)c);
    if (right.find('~') == std::string::npos) continue;
    std::vector<std::string> parts;
    std::stringstream ps(right);
    std::string p;
    while (std::getline(ps, p, '~')) parts.push_back(p);
    if (parts.size() < 6) continue;
    Quote q;
    q.name = utf8_from_gbk(parts[1]);
    q.price = to_f(parts[3]);
    if (parts.size() > 4) q.prev_close = to_f(parts[4]);
    if (parts.size() > 5) q.open = to_f(parts[5]);
    if (parts.size() > 6) q.volume = to_f(parts[6]);
    if (parts.size() > 33) q.high = to_f(parts[33]);
    if (parts.size() > 34) q.low = to_f(parts[34]);
    if (parts.size() > 37) q.amount = to_f(parts[37]);
    // ~9/11/13/15/17 bid1..5 ; ~19/21/23/25/27 ask1..5
    for (int i = 0; i < 5; ++i) {
      size_t bi = 9 + static_cast<size_t>(i) * 2;
      size_t ai = 19 + static_cast<size_t>(i) * 2;
      if (parts.size() > bi) q.bids[i] = to_f(parts[bi]);
      if (parts.size() > ai) q.asks[i] = to_f(parts[ai]);
      if (parts.size() > bi + 1) q.bid_vols[i] = to_f(parts[bi + 1]);
      if (parts.size() > ai + 1) q.ask_vols[i] = to_f(parts[ai + 1]);
    }
    q.bid = q.bids[0];
    q.ask = q.asks[0];
    auto it = mapping.find(sym);
    q.code = it != mapping.end() ? it->second : normalize_code(parts.size() > 2 ? parts[2] : sym);
    // 基金/停牌也可能只要名称；有价或有名都收下
    if (q.price > 0 || !q.name.empty()) out[q.code] = q;
  }
  return true;
}

}  // namespace grid
