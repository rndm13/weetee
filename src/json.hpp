#pragma once
#include "exception"
#include "string"

// returns an error message, if there isn't returns null pointer
const char* json_format(std::string* json) noexcept;
