#pragma once

#include <filesystem>
#include <string>

namespace desk {

std::filesystem::path find_project_root();
std::string narrow(const std::filesystem::path& p);
std::wstring widen(const std::string& s);

}  // namespace desk
