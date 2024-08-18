#include "gtest/gtest.h"
#include "../../src/json.hpp"

TEST(json, json_format) {
    std::string input = R"json({"test": [1, "one", true]})json";
    std::string expected = R"json(
{
    "test": [
        1,
        "one",
        true
    ]
}
    )json";

    EXPECT_EQ(json_format(input), nullptr);
    EXPECT_TRUE(str_contains(expected, input));

    std::string invalid = "[";
    EXPECT_STREQ(json_format(invalid), "Invalid JSON");
}

TEST(json, json_compare) {
    std::string invalid = "[";
    std::string valid = R"json({
        "json_valid": true
    })json";
    std::string different = R"json({
        "json_different": true
    })json";

    EXPECT_STREQ(json_compare(invalid, valid), "Invalid Expected JSON");
    EXPECT_STREQ(json_compare(valid, invalid), "Invalid Response JSON");
    EXPECT_STREQ(json_compare(valid, different), "Unexpected Response JSON");
    EXPECT_EQ(json_compare(valid, valid), nullptr);
}
