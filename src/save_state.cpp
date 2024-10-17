#include "save_state.hpp"

bool SaveState::can_offset(size_t offset) {
    return this->load_idx + offset <= original_buffer.size();
}

char* SaveState::load_offset(size_t offset) {
    assert(this->load_idx + offset <= original_buffer.size());
    return original_buffer.data() + this->load_idx + offset;
}

void SaveState::save(const std::string& str) {
    size_t length = str.length();
    this->save(reinterpret_cast<char*>(&length), sizeof(length));
    if (length > 0) {
        this->save(str.data(), str.length());
    }
    return;
}

bool SaveState::can_load(const std::string& str) {
    size_t length = 0;
    if (!this->can_load_reset(length)) {
        return false;
    }
    this->load(length);

    if (length > 0 && !this->can_load(str.data(), length)) {
        return false;
    }

    return true;
}

void SaveState::load(std::string& str) {
    size_t length;
    this->load(length);
    if (length > 0) { // To avoid failing 0 size assertion in load
        str.resize(length);
        this->load(str.data(), length);
    }
    return;
}

void SaveState::finish_save() {
    this->original_size = this->original_buffer.size();
    this->original_buffer.shrink_to_fit();
}

void SaveState::reset_load() { this->load_idx = 0; }

bool SaveState::write(std::ostream& os) const {
    assert(os);
    assert(this->original_size > 0);
    assert(this->original_buffer.size() == this->original_size);

    if (this->original_size > SAVE_STATE_MAX_SIZE) {
        return false;
    }

    os.write(reinterpret_cast<const char*>(&this->save_version), sizeof(this->save_version));
    os.write(reinterpret_cast<const char*>(&this->original_size), sizeof(this->original_size));
    os.write(this->original_buffer.data(), static_cast<int32_t>(this->original_buffer.size()));
    os.flush();

    return true;
}

bool SaveState::read(std::istream& is) {
    assert(is);
    is.read(reinterpret_cast<char*>(&this->save_version), sizeof(this->save_version));
    if (!is || is.eof()) {
        return false;
    }
    is.read(reinterpret_cast<char*>(&this->original_size), sizeof(this->original_size));

    assert(this->original_size > 0);
    if (!is || is.eof() || this->original_size > SAVE_STATE_MAX_SIZE) {
        return false;
    }

    this->original_buffer.reserve(this->original_size);
    this->original_buffer.resize(this->original_size);

    is.read(this->original_buffer.data(), static_cast<int32_t>(this->original_size));

    return true;
}
