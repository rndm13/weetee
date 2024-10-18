#include "gtest/gtest.h"
#include <variant>
#include <optional>

#include "../../src/utils.hpp"

TEST(utils, vector_comparison) {
    std::vector<int> a = {1, 2, 4};
    std::vector<int> b = {2, 4, 3};
    std::vector<int> c = {2, 4, 3};

    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_EQ(b, c);
    EXPECT_EQ(c, b);
}

TEST(utils, find_and_replace) {
    std::string str = "replace this but keep old information.";

    str_find_and_replace(str, "replace this", "find new information");

    EXPECT_EQ(str, "find new information but keep old information.");
}

TEST(utils, str_contains) {
    std::string haystack = "Big Information";

    EXPECT_TRUE(str_contains(haystack, "INformAtion"));
    EXPECT_TRUE(str_contains(haystack, "big informAtion"));
    EXPECT_FALSE(str_contains(haystack, "big MISinformAtion"));
    EXPECT_FALSE(str_contains(haystack, "biformation"));
}

TEST(utils, split_string) {
    std::string str = "a, b, c,d";
    std::vector<std::string> splitted = split_string(str, ",");
    std::vector<std::string> expected = {"a", "b", "c", "d"};
    EXPECT_EQ(splitted, expected);
}

TEST(utils, encapsulation_ranges) {
    std::string str_vars = "Hello {var1} World! {{nested}_variable}";

    std::vector<Range> vars = encapsulation_ranges(str_vars, '{', '}');
    std::vector<Range> expected = {{6, 5}, {20, 18}, {21, 7}};

    EXPECT_EQ(vars, expected);
}

TEST(utils, get_filename) {
#ifndef WIN32
    std::string file_path = "/path/to/a/file.txt";
#else
    std::string file_path = "C:\\path\\to\\a\\file.txt";
#endif

    EXPECT_EQ(get_filename(file_path), "file.txt");
}


TEST(utils, MapKeyIterator) {
    std::unordered_map<std::string, std::string> map = {
        {"one", "1"},
        {"two", "2"},
        {"three", "3"},
    };

    for (auto it = MapKeyIterator<std::string, std::string>(map.begin()); it != MapKeyIterator<std::string, std::string>(map.end()); it++) {
        EXPECT_TRUE(map.contains(*it));
    }
}

TEST(utils, variant_from_index) {
    using Var = std::variant<std::string, int32_t>;
    EXPECT_TRUE(variant_from_index<Var>(0).has_value());
    EXPECT_TRUE(variant_from_index<Var>(1).has_value());
    EXPECT_FALSE(variant_from_index<Var>(2).has_value());
}
