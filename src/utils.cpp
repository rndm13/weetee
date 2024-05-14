#include "utils.hpp"

bool contains(const std::string& haystack, const std::string& needle) noexcept {
    size_t need_idx = 0;
    for (char hay : haystack) {
        if (std::tolower(hay) == std::tolower(needle[need_idx])) {
            need_idx++;
        }
        if (need_idx >= needle.size()) {
            return true;
        }
    }
    return false;
}

std::string get_filename(const std::string& path) noexcept {
#ifdef WIN32
    size_t slash = path.rfind("/");
#else
    size_t slash = path.rfind("\\");
#endif

    if (slash == std::string::npos) {
        slash = 0;
    } else {
        slash += 1; // skip over it
    }
    return path.substr(slash);
}

std::vector<std::string> split_string(const std::string& str,
                                      const std::string& separator) noexcept {
    std::vector<std::string> result;

    auto push_result = [&result, &str](size_t begin, size_t end) {
        assert(begin <= str.size());
        assert(end <= str.size());

        // Trimming
        while (begin < str.size() && std::isspace(str.at(begin))) {
            begin += 1;
        }
        // End overflows
        while (end - 1 < str.size() && std::isspace(str.at(end - 1))) {
            end -= 1;
        }

        result.push_back(str.substr(begin, end - begin));
    };

    size_t index = 0;
    do {
        size_t sep_idx = str.find(separator, index);

        if (sep_idx == index) {
            // Rare condition when separator is the first character
            result.push_back("");
            index = sep_idx + separator.size();
            continue;
        }

        if (sep_idx == std::string::npos) {
            push_result(index, str.size());
            break;
        }

        push_result(index, sep_idx);
        index = sep_idx + separator.size();
    } while (index < str.size());

    return result;
}

std::vector<std::pair<size_t, size_t>> encapsulation_ranges(std::string str, char begin,
                                                            char end) noexcept {
    std::vector<std::pair<size_t, size_t>> result;

    size_t index = 0;
    for (char c : str) {
        if (c == begin) {
            result.emplace_back(index, std::string::npos);
        }

        if (c == end) {
            for (auto it = result.rbegin(); it != result.rend(); it++) {
                if (it->second == std::string::npos) {
                    it->second = index - it->first;
                    break;
                }
            }
        }

        index += 1;
    };

    return result;
}

void find_and_replace(std::string& str, const std::string& to_replace,
                      const std::string& replace_with) noexcept {
    size_t found = str.find(to_replace);
    while (found != std::string::npos) {
        str.replace(found, to_replace.size(), replace_with);
        found = str.find(to_replace);
    }
};
