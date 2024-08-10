#pragma once

#include "json.hpp"

#define WEETEE_VARIABLE_KEY "__weetee_variable"

using VariablesMap = std::unordered_map<std::string, std::string>;

// TODO: Warn user when making nested variables

static constexpr size_t REPLACE_VARIABLES_MAX_NEST = 1;
std::string replace_variables(const VariablesMap& vars, const std::string& target) noexcept;
nlohmann::json pack_variables(std::string, const VariablesMap& vars) noexcept;
std::string unpack_variables(const nlohmann::json& schema, size_t indent) noexcept;

const char* json_format_variables(std::string& input, const VariablesMap& vars) noexcept;
