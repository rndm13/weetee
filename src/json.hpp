#pragma once

#include "nlohmann/json.hpp"

#include "utils.hpp"

#include "string"
#include "variant"

// returns an error message, if there isn't returns null pointer
const char* json_format(std::string* json) noexcept;
const char* json_validate(const std::string*, const std::string*) noexcept;

// partial specialization (full specialization works too)
namespace nlohmann {
template <class... Args> struct adl_serializer<std::variant<Args...>> {
    static void to_json(json& j, const std::variant<Args...>& variant) {
        std::visit([&j](const auto& v) { j = v; }, variant);
    }

    template <class Head, class... Tail>
    static void from_json(const json& j, std::variant<Head, Tail...>& variant) {
        try {
            variant = j.template get<Head>();
        } catch (json::type_error&) {
            std::variant<Tail...> eliminated;
            adl_serializer<std::variant<Args...>>::from_json<Tail...>(j, eliminated);

            variant = variant_cast(eliminated);
        }
    }

    template <class Head> static void from_json(const json& j, std::variant<Head>& variant) {
        variant = j.template get<Head>();
    }
};
} // namespace nlohmann
