#include "utils.hpp"

std::vector<std::string> split_string(const std::string& str, const std::string& separator) {
    std::vector<std::string> result;

    auto it = SplitStringIterator(str, separator);

    while (it.has_next()) {
        result.push_back(std::string(sv_trim(it.next())));
    }

    return result;
}

// Returns a vector of pairs of index and size of found range in string including begin and end
std::vector<Range> encapsulation_ranges(std::string str, char begin, char end) {
    std::vector<Range> result;

    size_t index = 0;
    for (char c : str) {
        if (c == begin) {
            result.emplace_back(index, std::string::npos);
        }

        if (c == end) {
            for (auto it = result.rbegin(); it != result.rend(); it++) {
                if (it->size == std::string::npos) {
                    it->size = index - it->idx;
                    break;
                }
            }
        }

        index += 1;
    };

    // Erase unescaped
    std::erase_if(result, [](const Range& it) { return it.size == std::string::npos; });

    return result;
}
