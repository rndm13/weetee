#pragma once

#include "json.hpp"

using VariablesMap = std::unordered_map<std::string, std::string>;

static constexpr size_t REPLACE_VARIABLES_MAX_NEST = 10;
std::string replace_variables(const VariablesMap& vars, const std::string& target) noexcept;
nlohmann::json pack_variables(const std::string&) noexcept;
std::string unpack_variables(const nlohmann::json& schema, size_t indent) noexcept;

const char* json_format_variables(std::string& input, const VariablesMap& vars) noexcept;
