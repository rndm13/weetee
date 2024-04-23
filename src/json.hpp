#pragma once

#include "nlohmann/json.hpp"
#include "string"
#include "variant"
using json = nlohmann::json;

// returns an error message, if there isn't returns null pointer
const char* json_format(std::string* json) noexcept;
const char* json_validate(const std::string*, const std::string*) noexcept;

template <class Variant> constexpr bool valid_variant_from_index(size_t index) noexcept {
    return index < std::variant_size_v<Variant>;
}

template <class Variant, size_t I = 0> constexpr Variant variant_from_index(size_t index) noexcept {
    if constexpr (valid_variant_from_index<Variant>(I)) {
        return index == 0 ? Variant{std::in_place_index<I>}
                          : variant_from_index<Variant, I + 1>(index - 1);
    }
    assert(false && "Invalid variant index");
}

template <class... Args> struct variant_cast_proxy {
    std::variant<Args...> v;

    template <class... ToArgs> operator std::variant<ToArgs...>() const {
        return std::visit([](auto&& arg) -> std::variant<ToArgs...> { return arg; }, v);
    }
};

template <class... Args>
auto variant_cast(const std::variant<Args...>& v) -> variant_cast_proxy<Args...> {
    return {v};
}

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
