#include "grid/expr.hpp"

#include <cctype>
#include <cmath>
#include <stdexcept>

namespace grid {
namespace {

struct Parser {
  const std::string& s;
  const std::unordered_map<std::string, double>& env;
  size_t i = 0;

  void skip() {
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
  }

  bool eat(const char* kw) {
    skip();
    size_t n = 0;
    while (kw[n]) ++n;
    if (i + n > s.size()) return false;
    for (size_t k = 0; k < n; ++k) {
      char c = s[i + k];
      if (std::toupper((unsigned char)c) != kw[k]) return false;
    }
    if (i + n < s.size() && std::isalnum((unsigned char)s[i + n])) return false;
    i += n;
    return true;
  }

  double primary() {
    skip();
    if (i >= s.size()) throw std::runtime_error("表达式不完整");
    if (s[i] == '(') {
      ++i;
      double v = logic_or();
      skip();
      if (i >= s.size() || s[i] != ')') throw std::runtime_error("缺少右括号");
      ++i;
      return v;
    }
    if (std::isdigit((unsigned char)s[i]) || s[i] == '.') {
      size_t start = i;
      while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.')) ++i;
      return std::stod(s.substr(start, i - start));
    }
    if (std::isalpha((unsigned char)s[i]) || s[i] == '_') {
      size_t start = i;
      while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) ++i;
      std::string name = s.substr(start, i - start);
      for (auto& c : name) c = (char)std::toupper((unsigned char)c);
      auto it = env.find(name);
      return it == env.end() ? 0 : it->second;
    }
    throw std::runtime_error("无法解析: " + s.substr(i, 12));
  }

  double unary() {
    skip();
    if (i < s.size() && s[i] == '-') {
      ++i;
      return -unary();
    }
    return primary();
  }

  double mul() {
    double v = unary();
    for (;;) {
      skip();
      if (i < s.size() && (s[i] == '*' || s[i] == '/')) {
        char op = s[i++];
        double r = unary();
        if (op == '*') v *= r;
        else {
          if (r == 0) throw std::runtime_error("除零");
          v /= r;
        }
      } else
        break;
    }
    return v;
  }

  double add() {
    double v = mul();
    for (;;) {
      skip();
      if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        char op = s[i++];
        double r = mul();
        v = op == '+' ? v + r : v - r;
      } else
        break;
    }
    return v;
  }

  double cmp() {
    double v = add();
    skip();
    std::string op;
    if (i + 1 < s.size() && (s[i] == '<' || s[i] == '>' || s[i] == '=' || s[i] == '!') && s[i + 1] == '=') {
      op.assign(s, i, 2);
      i += 2;
    } else if (i < s.size() && (s[i] == '<' || s[i] == '>' || s[i] == '=')) {
      op = s.substr(i, 1);
      ++i;
    } else {
      return v;
    }
    double r = add();
    bool ok = false;
    if (op == "<") ok = v < r;
    else if (op == "<=") ok = v <= r;
    else if (op == ">") ok = v > r;
    else if (op == ">=") ok = v >= r;
    else if (op == "=" || op == "==") ok = v == r;
    else if (op == "!=") ok = v != r;
    return ok ? 1 : 0;
  }

  double logic_and() {
    double v = cmp();
    while (eat("AND")) {
      double r = cmp();
      v = (v != 0 && r != 0) ? 1 : 0;
    }
    return v;
  }

  double logic_or() {
    double v = logic_and();
    while (eat("OR")) {
      double r = logic_and();
      v = (v != 0 || r != 0) ? 1 : 0;
    }
    return v;
  }
};

}  // namespace

bool eval_expr(const std::string& expr, const std::unordered_map<std::string, double>& env, bool& out,
               std::string& err) {
  try {
    Parser p{expr, env, 0};
    double v = p.logic_or();
    p.skip();
    if (p.i != expr.size()) {
      err = "表达式有多余内容";
      return false;
    }
    out = v != 0 && !std::isnan(v);
    return true;
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
}

}  // namespace grid
