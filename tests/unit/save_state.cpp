#include "../../src/save_state.hpp"
#include "gtest/gtest.h"

struct TestData {
    std::variant<int32_t, std::string> id = 103;
    std::string name = "Sasha";
    std::string password = "12345678";
    std::vector<int64_t> points = {0, 2, 3, 5};
    std::optional<int64_t> average_points = 3;
    std::unordered_map<int32_t, std::string> dictionary = {{1, "one"}, {2, "two"}, {3, "three"}};

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(this->id);
        save->save(this->name);
        save->save(this->password);
        save->save(this->points);
        save->save(this->average_points);
        save->save(this->dictionary);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(this->id)) {
            return false;
        }
        if (!save->can_load(this->name)) {
            return false;
        }
        if (!save->can_load(this->password)) {
            return false;
        }
        if (!save->can_load(this->points)) {
            return false;
        }
        if (!save->can_load(this->average_points)) {
            return false;
        }
        if (!save->can_load(this->dictionary)) {
            return false;
        }

        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(this->id);
        save->load(this->name);
        save->load(this->password);
        save->load(this->points);
        save->load(this->average_points);
        save->load(this->dictionary);
    }
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

TEST(save_state, can_load) {
    // std::unordered_map<int32_t, std::string> data = {{1, "one"}, {2, "two"}, {3, "three"}};

    // SaveState ss = {};
    // ss.save(data);
    // ss.finish_save();
    // ss.reset_load();

    // std::unordered_map<int32_t, std::string> got = {};

    // ASSERT_TRUE(ss.can_load(got) && ss.load_idx == ss.original_size);
    // ss.reset_load();

    // ss.original_buffer[0] = 123;
    // ss.original_buffer[1] = 103;
    // ss.original_buffer[2] = 23;

    // ASSERT_FALSE(ss.can_load(got) && ss.load_idx == ss.original_size);
    // ss.reset_load();

    TestData input = {};

    SaveState ss = {};
    ss.save(input);
    ss.finish_save();
    ss.reset_load();

    TestData got = {};

    ASSERT_TRUE(ss.can_load(got) && ss.load_idx == ss.original_size);
    ss.reset_load();

    ss.original_buffer[0] = 123;
    ss.original_buffer[1] = 103;
    ss.original_buffer[2] = 23;

    ASSERT_FALSE(ss.can_load(got) && ss.load_idx == ss.original_size);
    ss.reset_load();
}

TEST(save_state, load) {
    TestData input = {};

    SaveState ss = {};
    ss.save(input);
    ss.finish_save();
    ss.reset_load();

    TestData got = {};

    ASSERT_TRUE(ss.can_load(got) && ss.load_idx == ss.original_size);
    ss.reset_load();
    ss.load(got);
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

    ASSERT_TRUE(ss.can_load(got) && ss.load_idx == ss.original_size);
    ss.reset_load();
    ss.load(got);
    EXPECT_EQ(input, got);
}
