#include "desk/portfolios.hpp"

#include "desk/settings.hpp"
#include "grid/http_win.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

namespace desk {
namespace {

double jnum(const nlohmann::json& j, const char* key) {
  if (!j.contains(key) || j[key].is_null()) return 0;
  if (j[key].is_number()) return j[key].get<double>();
  if (j[key].is_string()) {
    try {
      return std::stod(j[key].get<std::string>());
    } catch (...) {
      return 0;
    }
  }
  return 0;
}

std::string guess_market(const std::string& num) {
  if (num.empty()) return "SZ";
  if (num[0] == '6' || num[0] == '5' || num[0] == '9') return "SH";
  if (num[0] == '8' || num[0] == '4') return "BJ";
  return "SZ";
}

std::string norm_code(std::string raw) {
  for (auto& c : raw) c = (char)toupper((unsigned char)c);
  if (raw.find('.') != std::string::npos) return raw;
  if (raw.size() == 6) return raw + "." + guess_market(raw);
  return raw;
}

std::string em_get(const std::string& query, std::string& err) {
  std::vector<std::wstring> headers = {
      L"Referer: https://group.eastmoney.com/mcombin.html",
      L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
      L"Accept: application/json, text/plain, */*",
  };
  std::string path = "/qry_tzzh_v2?" + query;
  return grid::winhttp_get(L"simqry2.eastmoney.com", grid::utf16_from_utf8(path), err, headers);
}

std::string url_encode(const std::string& s) {
  std::ostringstream oss;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') oss << c;
    else {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "%%%02X", c);
      oss << buf;
    }
  }
  return oss.str();
}

const char* rank_syl(const std::string& rank) {
  if (rank == "5d") return "10005";
  if (rank == "20d") return "10020";
  if (rank == "250d") return "10250";
  if (rank == "total") return "10000";
  return "10001";
}

}  // namespace

std::string xq_get(const std::string& path_and_query, const std::string& cookie, std::string& err) {
  std::vector<std::wstring> headers = {
      L"Referer: https://xueqiu.com/",
      L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
      L"Accept: application/json, text/plain, */*",
      L"X-Requested-With: XMLHttpRequest",
  };
  if (!cookie.empty()) {
    headers.push_back(L"Cookie: " + grid::utf16_from_utf8(cookie));
  }
  return grid::winhttp_get(L"xueqiu.com", grid::utf16_from_utf8(path_and_query), err, headers);
}

int xq_category(const std::string& rank) {
  if (rank == "20d") return 13;
  if (rank == "250d") return 14;
  return 12;
}

std::string portfolios_list_json(const std::string& source, const std::string& rank, int page, int size,
                                 const std::string& q, int& http_status, std::string& err) {
  http_status = 200;
  if (source == "xueqiu") {
    auto cookie = SettingsStore::instance().load().xueqiu_cookie;
    if (cookie.empty()) {
      http_status = 401;
      err = "xueqiu cookie not set";
      return nlohmann::json{{"detail", err}}.dump();
    }
    std::ostringstream qs;
    qs << "/cubes/discover/rank/cube/list.json?category=" << xq_category(rank) << "&market=cn&page=" << page
       << "&count=" << size;
    std::string body = xq_get(qs.str(), cookie, err);
    if (body.empty()) {
      http_status = 502;
      return nlohmann::json{{"detail", err.empty() ? "xueqiu request failed" : err}}.dump();
    }
    if (body.find("400016") != std::string::npos || body.find("重新登录") != std::string::npos) {
      http_status = 401;
      return nlohmann::json{{"detail", "xueqiu cookie expired"}}.dump();
    }
    try {
      auto root = nlohmann::json::parse(body);
      nlohmann::json items = nlohmann::json::array();
      auto rows = root.contains("list") ? root["list"] : root.value("cubes", nlohmann::json::array());
      for (const auto& row : rows) {
        std::string id = row.value("symbol", row.value("cube_symbol", ""));
        std::string name = row.value("name", row.value("cube_name", id));
        std::string owner;
        if (row.contains("owner") && row["owner"].is_object()) owner = row["owner"].value("screen_name", "");
        else owner = row.value("screen_name", "");
        nlohmann::json card = {
            {"source", "xueqiu"},
            {"id", id},
            {"name", name},
            {"owner", owner},
            {"daily_return", jnum(row, "daily_gain")},
            {"return_5d", jnum(row, "weekly_gain")},
            {"return_20d", jnum(row, "monthly_gain")},
            {"return_250d", jnum(row, "annualized_gain_rate")},
            {"total_return", jnum(row, "total_gain")},
            {"followers", (int)jnum(row, "follower_count")},
            {"rank", row.contains("rank") && !row["rank"].is_null() ? row["rank"] : nullptr},
        };
        if (!q.empty()) {
          std::string key = q;
          for (auto& c : key) c = (char)tolower((unsigned char)c);
          std::string hay = id + name + owner;
          for (auto& c : hay) c = (char)tolower((unsigned char)c);
          if (hay.find(key) == std::string::npos) continue;
        }
        items.push_back(card);
      }
      int total = root.value("totalCount", root.value("total", (int)items.size()));
      return nlohmann::json{{"source", "xueqiu"}, {"total", total}, {"page", page}, {"items", items}}.dump();
    } catch (const std::exception& ex) {
      http_status = 502;
      return nlohmann::json{{"detail", ex.what()}}.dump();
    }
  }
  if (source != "eastmoney") {
    http_status = 400;
    err = "unknown source";
    return nlohmann::json{{"detail", err}}.dump();
  }

  int rec_idx = (page < 1 ? 0 : page - 1) * size + 1;
  std::ostringstream qs;
  qs << "plat=2&ver=web20&utToken=&ctToken=&type=spo_rank_syl&sylType=" << rank_syl(rank)
     << "&recIdx=" << rec_idx << "&recCnt=" << size;
  std::string body = em_get(qs.str(), err);
  if (body.empty()) {
    http_status = 502;
    return nlohmann::json{{"detail", err}}.dump();
  }
  try {
    auto root = nlohmann::json::parse(body);
    nlohmann::json items = nlohmann::json::array();
    for (const auto& row : root.value("data", nlohmann::json::array())) {
      std::string id;
      if (row.contains("zjzh")) {
        if (row["zjzh"].is_string()) id = row["zjzh"].get<std::string>();
        else if (row["zjzh"].is_number_integer()) id = std::to_string(row["zjzh"].get<long long>());
        else if (row["zjzh"].is_number()) id = std::to_string((long long)row["zjzh"].get<double>());
      }
      std::string owner;
      if (row.contains("username") && row["username"].is_string()) owner = row["username"].get<std::string>();
      else if (row.contains("userid")) {
        if (row["userid"].is_string()) owner = row["userid"].get<std::string>();
        else if (row["userid"].is_number()) owner = std::to_string((long long)jnum(row, "userid"));
      }
      nlohmann::json card = {
          {"source", "eastmoney"},
          {"id", id},
          {"name", row.value("zuheName", "")},
          {"owner", owner},
          {"daily_return", jnum(row, "syl_dr")},
          {"return_5d", jnum(row, "syl_5r")},
          {"return_20d", jnum(row, "syl_20r")},
          {"return_250d", jnum(row, "syl_250r")},
          {"total_return", jnum(row, "zsyl")},
          {"followers", (int)jnum(row, "concerned")},
          {"last_action", row.value("mmbz", "")},
          {"last_stock", row.value("stkName", "")},
          {"rank", (int)jnum(row, "rank")},
      };
      if (!q.empty()) {
        std::string key = q;
        for (auto& c : key) c = (char)tolower((unsigned char)c);
        std::string hay = card["id"].get<std::string>() + card["name"].get<std::string>() +
                          (card["owner"].is_string() ? card["owner"].get<std::string>() : "");
        for (auto& c : hay) c = (char)tolower((unsigned char)c);
        if (hay.find(key) == std::string::npos) continue;
      }
      items.push_back(card);
    }
    int total = root.value("totalCnt", (int)items.size());
    return nlohmann::json{{"source", "eastmoney"}, {"total", total}, {"page", page}, {"items", items}}.dump();
  } catch (const std::exception& ex) {
    http_status = 502;
    err = ex.what();
    return nlohmann::json{{"detail", err}}.dump();
  }
}

std::string portfolio_detail_json(const std::string& source, const std::string& id, int& http_status,
                                  std::string& err) {
  http_status = 200;
  if (source == "xueqiu") {
    auto cookie = SettingsStore::instance().load().xueqiu_cookie;
    if (cookie.empty()) {
      http_status = 401;
      return nlohmann::json{{"detail", "xueqiu cookie not set"}}.dump();
    }
    std::string symbol = id;
    for (auto& c : symbol) c = (char)toupper((unsigned char)c);
    if (symbol.size() >= 2 && symbol.compare(0, 2, "ZH") != 0) {
      bool digits = !symbol.empty() && symbol.find_first_not_of("0123456789") == std::string::npos;
      if (digits) symbol = "ZH" + symbol;
    }
    std::string qbody =
        xq_get("/cubes/rebalancing/current.json?cube_symbol=" + url_encode(symbol), cookie, err);
    if (qbody.empty()) {
      http_status = 502;
      return nlohmann::json{{"detail", err}}.dump();
    }
    if (qbody.find("400016") != std::string::npos || qbody.find("重新登录") != std::string::npos) {
      http_status = 401;
      return nlohmann::json{{"detail", "xueqiu cookie expired"}}.dump();
    }
    try {
      auto cur = nlohmann::json::parse(qbody);
      auto last_rb = cur.contains("last_rb") ? cur["last_rb"] : cur.value("data", nlohmann::json::object()).value("last_rb", nlohmann::json::object());
      nlohmann::json holdings = nlohmann::json::array();
      double cash_w = jnum(last_rb, "cash");
      for (const auto& row : last_rb.value("holdings", nlohmann::json::array())) {
        double w = jnum(row, "weight");
        if (w <= 0) continue;
        std::string code = norm_code(row.value("stock_symbol", row.value("stock_id", "")));
        holdings.push_back({{"code", code},
                            {"name", row.value("stock_name", "")},
                            {"weight", std::round(w * 100) / 100.0},
                            {"prev_weight", jnum(row, "prev_weight")},
                            {"price", jnum(row, "price")},
                            {"market", guess_market(code.substr(0, code.find('.')))}});
      }
      std::string quote_body = xq_get("/cubes/quote.json?code=" + url_encode(symbol), cookie, err);
      nlohmann::json quote;
      if (!quote_body.empty()) {
        try {
          auto qr = nlohmann::json::parse(quote_body);
          if (qr.contains(symbol) && qr[symbol].is_object()) quote = qr[symbol];
        } catch (...) {
        }
      }
      double pos_w = 0;
      for (auto& h : holdings) pos_w += h.value("weight", 0.0);
      if (cash_w <= 0) cash_w = std::max(0.0, 100.0 - pos_w);
      nlohmann::json detail = {
          {"source", "xueqiu"},
          {"id", symbol},
          {"name", quote.value("name", symbol)},
          {"owner", ""},
          {"description", ""},
          {"nav", jnum(quote, "net_value")},
          {"cash_weight", std::round(cash_w * 100) / 100.0},
          {"position_weight", std::round(pos_w * 100) / 100.0},
          {"daily_return", jnum(quote, "daily_gain")},
          {"return_20d", jnum(quote, "monthly_gain")},
          {"return_250d", jnum(quote, "annualized_gain")},
          {"total_return", jnum(quote, "total_gain")},
          {"holdings", holdings},
          {"rebalances", nlohmann::json::array()},
          {"source_url", "https://xueqiu.com/P/" + symbol},
      };
      return detail.dump();
    } catch (const std::exception& ex) {
      http_status = 502;
      return nlohmann::json{{"detail", ex.what()}}.dump();
    }
  }
  if (source != "eastmoney") {
    http_status = 400;
    return nlohmann::json{{"detail", "unknown source"}}.dump();
  }

  std::string q1 = "plat=2&ver=web20&utToken=&ctToken=&type=spo_zuhe_detail_basic&zjzh=" + url_encode(id);
  std::string body = em_get(q1, err);
  if (body.empty()) {
    http_status = 502;
    return nlohmann::json{{"detail", err}}.dump();
  }
  try {
    auto root = nlohmann::json::parse(body);
    auto basic = root.value("data", nlohmann::json::object());
    if (basic.empty()) {
      http_status = 404;
      return nlohmann::json{{"detail", "portfolio not found"}}.dump();
    }
    double nav = jnum(basic, "zzc");
    std::string q2 = "plat=2&ver=web20&utToken=&ctToken=&type=spo_hold_detail&zjzh=" + url_encode(id) +
                     "&recIdx=1&recCnt=80";
    std::string hold_body = em_get(q2, err);
    nlohmann::json holdings = nlohmann::json::array();
    double pos_w = 0;
    if (!hold_body.empty()) {
      auto hr = nlohmann::json::parse(hold_body);
      for (const auto& row : hr.value("data", nlohmann::json::array())) {
        std::string code = norm_code(row.value("stkCode", ""));
        if (code.empty()) continue;
        double price = jnum(row, "price");
        int shares = (int)jnum(row, "holdPos");
        double mv = price * shares;
        holdings.push_back({
            {"code", code},
            {"name", row.value("stkName", "")},
            {"weight", 0},
            {"price", price},
            {"shares", shares},
            {"cost", jnum(row, "cbj")},
            {"pnl_pct", jnum(row, "ykRate")},
            {"market", guess_market(code.substr(0, code.find('.')))},
        });
        pos_w += mv;
      }
      double denom = nav > 0 ? nav : pos_w;
      if (denom > 0) {
        for (auto& h : holdings) {
          double mv = h["price"].get<double>() * h["shares"].get<int>();
          h["weight"] = std::round(mv / denom * 10000) / 100.0;
        }
      }
    }
    double position_weight = 0;
    for (auto& h : holdings) position_weight += h.value("weight", 0.0);
    nlohmann::json rebalances = nlohmann::json::array();
    std::string q3 = "plat=2&ver=web20&utToken=&ctToken=&type=spo_hldchg_home&zjzh=" + url_encode(id) +
                     "&recIdx=1&recCnt=20";
    std::string rb_body = em_get(q3, err);
    if (!rb_body.empty()) {
      try {
        auto rr = nlohmann::json::parse(rb_body);
        for (const auto& row : rr.value("data", nlohmann::json::array())) {
          rebalances.push_back({{"code", norm_code(row.value("stkCode", ""))},
                                {"name", row.value("stkName", "")},
                                {"side", row.value("mmbz", "")},
                                {"prev_weight", jnum(row, "holdPosBef")},
                                {"weight", jnum(row, "holdPosAft")},
                                {"price", jnum(row, "cjjg")},
                                {"time", row.value("cjsj", "")}});
        }
      } catch (...) {
      }
    }
    nlohmann::json detail = {
        {"source", "eastmoney"},
        {"id", id},
        {"name", basic.value("zuheName", basic.value("zhmcReal", id))},
        {"owner", basic.value("username", "")},
        {"description", basic.value("comment", "")},
        {"nav", nav},
        {"cash_weight", std::max(0.0, 100.0 - position_weight)},
        {"position_weight", position_weight},
        {"daily_return", jnum(basic, "syl_dr")},
        {"return_5d", jnum(basic, "syl_5r")},
        {"return_20d", jnum(basic, "syl_20r")},
        {"return_250d", jnum(basic, "syl_250r")},
        {"total_return", jnum(basic, "zsyl")},
        {"followers", (int)jnum(basic, "concerned")},
        {"holdings", holdings},
        {"rebalances", rebalances},
        {"source_url", "https://group.eastmoney.com/other," + id + ".html"},
    };
    return detail.dump();
  } catch (const std::exception& ex) {
    http_status = 502;
    err = ex.what();
    return nlohmann::json{{"detail", err}}.dump();
  }
}

}  // namespace desk
