#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace grid {

std::string winhttp_get(const std::wstring& host, const std::wstring& path_and_query, std::string& err,
                        const std::vector<std::wstring>& headers = {}, std::uint16_t port = 443);

std::wstring utf16_from_utf8(const std::string& s);
std::string utf8_from_gbk(const std::string& gbk);
std::string normalize_code(const std::string& raw);
std::string to_tencent_symbol(const std::string& code);
std::string to_sina_symbol(const std::string& code);
std::string to_secid(const std::string& code);

}  // namespace grid
