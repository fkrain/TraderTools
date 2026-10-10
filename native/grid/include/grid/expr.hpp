#pragma once

#include <string>
#include <unordered_map>

namespace grid {

// Snapshot expression: numbers, names, + - * / () comparisons, AND, OR.
// Unknown names are 0. On syntax error returns false and sets err.
bool eval_expr(const std::string& expr, const std::unordered_map<std::string, double>& env, bool& out,
               std::string& err);

}  // namespace grid
