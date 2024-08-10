#include "gtest/gtest.h"
#include "../../src/variables.hpp"

using json = nlohmann::json;

TEST(variables, pack_variables) {
    std::string var_json = R"json(
{
    "hello": {world},
    "number": 2
}
    )json";

    VariablesMap vars = {
        {"world", "world_val"},
    };

    json got = pack_variables(var_json, vars);
    json expected = json::object({
        {"hello", json::object(
            {
                {WEETEE_VARIABLE_KEY, "world"}
            })
        },
        {"number", 2},
    });

    EXPECT_EQ(got, expected);
}

TEST(variables, unpack_variables) {
    json j = json::object({
        {"hello", json::object(
            {
                {WEETEE_VARIABLE_KEY, "world"}
            })
        },
        {"number", 2},
        {"array", json::array({
            json::object({
                {
                    "in_array",
                    json::object({
                        {WEETEE_VARIABLE_KEY, "in_array_var"}
                    })
                }
            })
        })}
    });

    std::string got = unpack_variables(j, 4);
    std::string expected = R"json(
{
    "array": [
        {
            "in_array": {in_array_var}
        }
    ],
    "hello": {world},
    "number": 2
}
    )json";

    EXPECT_TRUE(str_contains(expected, got));
}

TEST(variables, replace_variables) {
    std::string j_str = R"json(
{
    "hello": {world}
}
    )json";

    VariablesMap vars = {
        {"world", "\"world_val\""},
    };

    std::string expected = R"json(
{
    "hello": "world_val"
}
    )json";

    std::string got = replace_variables(vars, j_str);

    EXPECT_EQ(expected, got);
}

TEST(variables, json_format_variables) {
    std::string j_str = R"json({"hello": {world}})json";

    VariablesMap vars = {
        {"world", "\"world_val\""},
    };

    std::string expected = R"json(
{
    "hello": {world}
}
    )json";

    const char* err = json_format_variables(j_str, vars);

    EXPECT_TRUE(err == nullptr);
    EXPECT_TRUE(str_contains(expected, j_str));
}
