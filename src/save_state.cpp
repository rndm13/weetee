#include "save_state.hpp"

bool SaveState::can_offset(size_t offset) noexcept {
    return this->load_idx + offset <= original_buffer.size();
}

char* SaveState::load_offset(size_t offset) noexcept {
    assert(this->load_idx + offset <= original_buffer.size());
    return original_buffer.data() + this->load_idx + offset;
}

void SaveState::save(const std::string& str) noexcept {
    if (str.length() > 0) { // To avoid failing 0 size assertion in save
        this->save(str.data(), str.length());
    }
    this->save('\0');
}

bool SaveState::can_load(const std::string& str) noexcept {
    size_t length = 0;
    while (this->can_offset(length) && *this->load_offset(length) != char(0)) {
        length++;
    }
    if (!this->can_offset(length)) {
        return false;
    }

    this->load_idx += length + 1; // Skip over null terminator
    return true;
}

void SaveState::load(std::string& str) noexcept {
    size_t length = 0;
    while (*this->load_offset(length) != char(0)) {
        length++;
    }

    if (length > 0) { // To avoid failing 0 size assertion in load
        str.resize(length);
        this->load(str.data(), length);
    }

    this->load_idx++; // Skip over null terminator
}

void SaveState::finish_save() noexcept {
    this->original_size = this->original_buffer.size();
    this->original_buffer.shrink_to_fit();
}

void SaveState::reset_load() noexcept { this->load_idx = 0; }

bool SaveState::write(std::ostream& os) const noexcept {
    assert(os);
    assert(this->original_size > 0);
    assert(this->original_buffer.size() == this->original_size);

    if (this->original_size > SAVE_STATE_MAX_SIZE) {
        return false;
    }

    os.write(reinterpret_cast<const char*>(&this->original_size), sizeof(size_t));
    os.write(this->original_buffer.data(), static_cast<int32_t>(this->original_buffer.size()));
    os.flush();

    return true;
}

bool SaveState::read(std::istream& is) noexcept {
    assert(is);
    is.read(reinterpret_cast<char*>(&this->original_size), sizeof(size_t));

    assert(this->original_size > 0);
    if (this->original_size > SAVE_STATE_MAX_SIZE) {
        return false;
    }

    this->original_buffer.reserve(this->original_size);
    this->original_buffer.resize(this->original_size);

    is.read(this->original_buffer.data(), static_cast<int32_t>(this->original_size));

    return true;
}
