#include "grid/bars.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <vector>

namespace grid {
namespace {

std::mutex g_mu;
struct MemBar {
  long long minute = 0;
  double o = 0, h = 0, l = 0, c = 0, v = 0;
};
std::unordered_map<std::string, std::vector<MemBar>> g_bars;

std::vector<double> closes_of(const std::vector<MemBar>& src, int minutes) {
  if (minutes < 1) minutes = 1;
  std::vector<double> out;
  if (src.empty()) return out;
  long long bucket = -1;
  double c = 0;
  for (const auto& b : src) {
    long long key = b.minute / minutes;
    if (key != bucket) {
      if (bucket >= 0) out.push_back(c);
      bucket = key;
    }
    c = b.c;
  }
  if (bucket >= 0) out.push_back(c);
  return out;
}

double sma_last(const std::vector<double>& c, int n) {
  if ((int)c.size() < n || n <= 0) return 0;
  double s = 0;
  for (int i = (int)c.size() - n; i < (int)c.size(); ++i) s += c[i];
  return s / n;
}

double ema_step(double prev, double x, int n) {
  double k = 2.0 / (n + 1.0);
  return prev == 0 ? x : prev * (1 - k) + x * k;
}

}  // namespace

void note_quote_bar(const std::filesystem::path& data_dir, const Quote& q, long long epoch_sec) {
  if (q.code.empty() || q.price <= 0 || epoch_sec <= 0) return;
  long long minute = epoch_sec / 60;
  std::lock_guard<std::mutex> lock(g_mu);
  auto& bars = g_bars[q.code];
  if (bars.empty() || bars.back().minute != minute) {
    MemBar b;
    b.minute = minute;
    b.o = q.price;
    b.h = q.price;
    b.l = q.price;
    b.c = q.price;
    b.v = q.volume;
    bars.push_back(b);
    if (bars.size() > 2400) bars.erase(bars.begin(), bars.begin() + (bars.size() - 2400));
    if (!data_dir.empty() && bars.size() % 20 == 0) {
      std::error_code ec;
      auto dir = data_dir / "bars";
      std::filesystem::create_directories(dir, ec);
      nlohmann::json arr = nlohmann::json::array();
      size_t from = bars.size() > 400 ? bars.size() - 400 : 0;
      for (size_t i = from; i < bars.size(); ++i) {
        arr.push_back({{"m", bars[i].minute}, {"o", bars[i].o}, {"h", bars[i].h}, {"l", bars[i].l}, {"c", bars[i].c}});
      }
      std::ofstream out(dir / (q.code + ".json"));
      if (out) out << arr.dump();
    }
  } else {
    auto& b = bars.back();
    b.h = std::max(b.h, q.price);
    b.l = std::min(b.l, q.price);
    b.c = q.price;
    if (q.volume > 0) b.v = q.volume;
  }
}

IndicatorSnap indicators_for(const std::filesystem::path&, const std::string& code, int minutes) {
  IndicatorSnap ind;
  std::vector<double> c;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_bars.find(code);
    if (it == g_bars.end()) return ind;
    c = closes_of(it->second, minutes);
  }
  ind.ma5 = sma_last(c, 5);
  ind.ma10 = sma_last(c, 10);
  ind.ma20 = sma_last(c, 20);
  ind.ma60 = sma_last(c, 60);
  if (c.size() >= 20) {
    double mid = sma_last(c, 20);
    double var = 0;
    for (int i = (int)c.size() - 20; i < (int)c.size(); ++i) {
      double d = c[i] - mid;
      var += d * d;
    }
    double sd = std::sqrt(var / 20.0);
    ind.boll_mid = mid;
    ind.boll_up = mid + 2 * sd;
    ind.boll_dn = mid - 2 * sd;
  }
  if (c.size() >= 35) {
    std::vector<double> dif, dea;
    double ef = 0, es = 0, ed = 0;
    for (double x : c) {
      ef = ema_step(ef, x, 12);
      es = ema_step(es, x, 26);
      double d = ef - es;
      ed = ema_step(ed, d, 9);
      dif.push_back(d);
      dea.push_back(ed);
    }
    auto hist = [&](size_t i) { return dif[i] - dea[i]; };
    size_t n = dif.size();
    if (hist(n - 2) <= 0 && hist(n - 1) > 0) ind.macd_cross_buy = 1;
    if (hist(n - 2) >= 0 && hist(n - 1) < 0) ind.macd_cross_sell = 1;
  }
  if (c.size() >= 12) {
    std::vector<double> k, d;
    double kv = 50, dv = 50;
    for (size_t i = 8; i < c.size(); ++i) {
      double hh = c[i], ll = c[i];
      for (size_t j = i - 8; j <= i; ++j) {
        hh = std::max(hh, c[j]);
        ll = std::min(ll, c[j]);
      }
      double rsv = hh == ll ? 50 : (c[i] - ll) / (hh - ll) * 100.0;
      kv = kv * 2.0 / 3.0 + rsv / 3.0;
      dv = dv * 2.0 / 3.0 + kv / 3.0;
      k.push_back(kv);
      d.push_back(dv);
    }
    if (k.size() >= 2) {
      size_t n = k.size();
      if (k[n - 2] <= d[n - 2] && k[n - 1] > d[n - 1]) ind.kdj_cross_buy = 1;
      if (k[n - 2] >= d[n - 2] && k[n - 1] < d[n - 1]) ind.kdj_cross_sell = 1;
    }
  }
  return ind;
}

void fill_indicator_env(const IndicatorSnap& ind, std::unordered_map<std::string, double>& env, int minutes) {
  std::string suf = "1M";
  if (minutes == 5) suf = "5M";
  else if (minutes == 15) suf = "15M";
  else if (minutes == 30) suf = "30M";
  else if (minutes == 60) suf = "60M";
  else if (minutes >= 240) suf = "1D";
  env["MA5_" + suf] = ind.ma5;
  env["MA10_" + suf] = ind.ma10;
  env["MA20_" + suf] = ind.ma20;
  env["MA60_" + suf] = ind.ma60;
  env["MACD_" + suf + "_CROSS_BUY"] = ind.macd_cross_buy;
  env["MACD_" + suf + "_CROSS_SELL"] = ind.macd_cross_sell;
  env["KDJ_" + suf + "_CROSS_BUY"] = ind.kdj_cross_buy;
  env["KDJ_" + suf + "_CROSS_SELL"] = ind.kdj_cross_sell;
  env["BOLL_UP_" + suf] = ind.boll_up;
  env["BOLL_MID_" + suf] = ind.boll_mid;
  env["BOLL_DN_" + suf] = ind.boll_dn;
  if (minutes == 1) {
    env["MA5"] = ind.ma5;
    env["MA20"] = ind.ma20;
    env["MACD_CROSS_BUY"] = ind.macd_cross_buy;
    env["MACD_CROSS_SELL"] = ind.macd_cross_sell;
    env["KDJ_CROSS_BUY"] = ind.kdj_cross_buy;
    env["KDJ_CROSS_SELL"] = ind.kdj_cross_sell;
    env["BOLL_UP"] = ind.boll_up;
    env["BOLL_MID"] = ind.boll_mid;
    env["BOLL_DN"] = ind.boll_dn;
  }
}

}  // namespace grid
