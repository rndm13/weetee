#include "../../src/save_state.hpp"
#include "gtest/gtest.h"

struct TestData {
    std::variant<int32_t, std::string> id = 103;
    std::string name = "Sasha";
    std::string password = "12345678";
    std::vector<int64_t> points = {0, 2, 3, 5};
    std::optional<int64_t> average_points = 3;
    std::unordered_map<int32_t, std::string> dictionary = {{1, "one"}, {2, "two"}, {3, "three"}};

    inline void save(SaveState& ss) const {
        if (0 <= 0 || ss.save_version >= 0) {
            ss.save(this->id);
        }
        if (0 <= 0 || ss.save_version >= 0) {
            ss.save(this->name);
        }
        if (0 <= 0 || ss.save_version >= 0) {
            ss.save(this->password);
        }
        if (0 <= 0 || ss.save_version >= 0) {
            ss.save(this->points);
        }
        if (0 <= 0 || ss.save_version >= 0) {
            ss.save(this->average_points);
        }
        if (0 <= 0 || ss.save_version >= 0) {
            ss.save(this->dictionary);
        }
    }
    inline bool load(SaveState& ss) {
        if (0 <= 0 || ss.save_version >= 0) {
            if (!ss.load(this->id)) {
                return false;
            }
        }
        if (0 <= 0 || ss.save_version >= 0) {
            if (!ss.load(this->name)) {
                return false;
            }
        }
        if (0 <= 0 || ss.save_version >= 0) {
            if (!ss.load(this->password)) {
                return false;
            }
        }
        if (0 <= 0 || ss.save_version >= 0) {
            if (!ss.load(this->points)) {
                return false;
            }
        }
        if (0 <= 0 || ss.save_version >= 0) {
            if (!ss.load(this->average_points)) {
                return false;
            }
        }
        if (0 <= 0 || ss.save_version >= 0) {
            if (!ss.load(this->dictionary)) {
                return false;
            }
        }
        return true;
    };
};

bool operator==(const TestData& first, const TestData& second) {
    return first.id == second.id && first.name == second.name &&
           first.password == second.password && first.points == second.points &&
           first.dictionary == second.dictionary;
}

TEST(save_state, save) {
    TestData input = {};

    SaveState ss = {};
    ss.save(input);
    ss.finish_save();
}

TEST(save_state, load) {
    TestData input = {};

    SaveState ss = {};
    ss.save(input);
    ss.finish_save();
    ss.reset_load();

    TestData got = {};
    ASSERT_TRUE(ss.load(got));
    EXPECT_EQ(ss.load_idx, ss.original_size);
    EXPECT_EQ(input, got);
}

TEST(save_state, load_alt_data) {
    TestData input = {
        .id = "uuid",
        .points = {},
        .average_points = std::nullopt,
        .dictionary = {},
    };

    SaveState ss = {};
    ss.save(input);
    ss.finish_save();
    ss.reset_load();

    TestData got = {};

    ASSERT_TRUE(ss.load(got));
    EXPECT_EQ(ss.load_idx, ss.original_size);
    EXPECT_EQ(input, got);
}
