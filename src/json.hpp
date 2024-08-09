#pragma once

#include "../external/json/single_include/nlohmann/json.hpp"

#include "i18n.hpp"
#include "utils.hpp"

#include "string"
#include "variant"

#define WEETEE_VARIABLE_KEY "__weetee_variable"

// returns an error message, if there isn't returns null pointer
const char* json_format(std::string& json) noexcept;
const char* json_compare(const std::string&, const std::string&) noexcept;

namespace nlohmann {
template <class... Args> struct adl_serializer<std::variant<Args...>> {
    static void to_json(json& j, const std::variant<Args...>& variant) {
        std::visit([&j](const auto& v) { j = v; }, variant);
    }

    template <class Head, class... Tail>
    static void from_json(const json& j, std::variant<Head, Tail...>& variant) {
        assert(!j.is_discarded());

        try {
            variant = j.template get<Head>();
        } catch (std::exception& e) {
            std::variant<Tail...> eliminated;
            from_json<Tail...>(j, eliminated);

            variant = variant_cast(eliminated);
        }
    }

    template <class Head> static void from_json(const json& j, std::variant<Head>& variant) {
        variant = j.template get<Head>();
    }
};
} // namespace nlohmann

void from_json(const nlohmann::json& j, I18N& i18n) noexcept;
