#pragma once

#include "utils.hpp"

#include "algorithm"
#include "cassert"
#include "cmath"
#include "cstdint"
#include "fstream"
#include "iterator"
#include "optional"
#include "string"
#include "type_traits"
#include "unordered_map"
#include "variant"
#include "vector"

static constexpr size_t SAVE_STATE_MAX_SIZE = 0x10000000;

struct SaveState {
    size_t save_version = {2};
    size_t original_size = {};
    size_t load_idx = {};
    std::vector<char> original_buffer;

    // helpers
    template <class T = void>
    void save(const char* ptr, size_t size = sizeof(T)) {
        assert(ptr);
        assert(size > 0);
        std::copy(ptr, ptr + size, std::back_inserter(this->original_buffer));
    }

    bool can_offset(size_t offset = 0);

    // modifies index, should be called before load and then reset
    template <class T = void>
    bool can_load(const char* ptr, size_t size = sizeof(T)) {
        assert(ptr);
        assert(size > 0);
        if (!can_offset(size)) {
            return false;
        }

        this->load_idx += size;
        return true;
    }

    template <class T = void>
    bool can_load_reset(const char* ptr, size_t size = sizeof(T)) {
        assert(ptr);
        assert(size > 0);
        if (!can_offset(size)) {
            return false;
        }

        return true;
    }

    char* load_offset(size_t offset = 0);

    template <class T = void>
    void load(char* ptr, size_t size = sizeof(T)) {
        assert(ptr);
        assert(size > 0);

        std::copy(this->load_offset(), this->load_offset(size), ptr);
        this->load_idx += size;
    }

    template <class T>
        requires(std::is_trivially_copyable<T>::value)
    void save(T trivial) {
        this->save<T>(reinterpret_cast<char*>(&trivial));
    }

    template <class T>
        requires(std::is_trivially_copyable<T>::value)
    bool can_load(const T& trivial) {
        return this->can_load(reinterpret_cast<const char*>(&trivial), sizeof(T));
    }

    template <class T>
        requires(std::is_trivially_copyable<T>::value)
    bool can_load_reset(const T& trivial) {
        return this->can_load_reset(reinterpret_cast<const char*>(&trivial), sizeof(T));
    }

    template <class T>
        requires(std::is_trivially_copyable<T>::value)
    void load(T& trivial) {
        this->load(reinterpret_cast<char*>(&trivial), sizeof(T));
    }

    void save(const std::string& str);
    bool can_load(const std::string& str);
    void load(std::string& str);

    void save(const std::monostate&) {}
    bool can_load(const std::monostate&) {
        return true;
    }
    void load(std::monostate&) {}

    template <class T>
    void save(const std::optional<T>& opt) {
        bool has_value = opt.has_value();
        this->save(has_value);

        if (has_value) {
            this->save(opt.value());
        }
    }

    template <class T>
    bool can_load(const std::optional<T>& opt) {
        bool has_value;
        this->load(has_value);

        if (has_value) {
            return this->can_load(T{});
        }

        return true;
    }

    template <class T>
    void load(std::optional<T>& opt) {
        bool has_value;
        this->load(has_value);

        if (has_value) {
            opt = T{};
            this->load(opt.value());
        } else {
            opt = std::nullopt;
        }
    }

    template <class K, class V>
    void save(const std::unordered_map<K, V>& map) {
        size_t size = map.size();
        this->save(size);

        for (const auto& [k, v] : map) {
            this->save(k);
            this->save(v);
        }
    }

    template <class K, class V>
    bool can_load(const std::unordered_map<K, V>&) {
        size_t size;
        if (!this->can_load_reset(size)) {
            return false;
        }
        this->load(size);
        if (size > SAVE_STATE_MAX_SIZE) {
            return false;
        }

        for (size_t i = 0; i < size; i++) {
            K k = {};
            V v = {};
            if (!this->can_load(k)) {
                return false;
            }
            if (!this->can_load(v)) {
                return false;
            }
        }

        return true;
    }

    template <class K, class V>
    void load(std::unordered_map<K, V>& map) {
        size_t size;
        this->load(size);

        map.clear();
        for (size_t i = 0; i < size; i++) {
            K k;
            V v;
            this->load(k);
            this->load(v);
            assert(!map.contains(k));
            map.emplace(k, v);
        }
    }

    template <class... T>
    void save(const std::variant<T...>& variant) {
        assert(variant.index() != std::variant_npos);
        size_t index = variant.index();
        this->save(index);

        std::visit([this](const auto& s) { this->save(s); }, variant);
    }

    template <class... T>
    bool can_load(const std::variant<T...>& variant) {
        size_t index;
        if (!this->can_load_reset(index)) {
            return false;
        }

        this->load(index);

        std::optional<std::variant<T...>> var = variant_from_index<std::variant<T...>>(index);

        if (!var.has_value()) {
            return false;
        };

        return std::visit([this](const auto& s) { return this->can_load(s); }, var.value());
    }

    template <class... T>
    void load(std::variant<T...>& variant) {
        size_t index;
        this->load(index);
        assert(index != std::variant_npos);

        auto variant_opt = variant_from_index<std::variant<T...>>(index);
        assert(variant_opt.has_value());

        variant = variant_opt.value();

        std::visit([this](auto& s) { this->load(s); }, variant);
    }

    template <class Element>
    void save(const std::vector<Element>& vec) {
        size_t size = vec.size();
        this->save(size);

        for (const auto& elem : vec) {
            this->save(elem);
        }
    }

    template <class Element>
    bool can_load(const std::vector<Element>& vec) {
        size_t size;
        if (!this->can_load_reset(size)) {
            return false;
        }
        this->load(size);
        if (size > SAVE_STATE_MAX_SIZE) {
            return false;
        }

        for (size_t i = 0; i < size; i++) {
            Element elem = {};
            if (!this->can_load(elem)) {
                return false;
            }
        }

        return true;
    }

    template <class Element>
    void load(std::vector<Element>& vec) {
        size_t size;
        this->load(size);

        vec.clear();
        if (size != 0) {
            vec.reserve(size);
        }
        vec.resize(size);

        for (auto& elem : vec) {
            this->load(elem);
        }
    }

    // YOU HAVE TO BE CAREFUL NOT TO PASS POINTERS!
    template <class T>
        requires(!std::is_trivially_copyable<T>::value)
    void save(const T& any) {
        any.save(this);
    }

    // YOU HAVE TO BE CAREFUL NOT TO PASS POINTERS!
    template <class T>
        requires(!std::is_trivially_copyable<T>::value)
    bool can_load(const T& any) {
        return any.can_load(this);
    }

    // YOU HAVE TO BE CAREFUL NOT TO PASS POINTERS!
    template <class T>
        requires(!std::is_trivially_copyable<T>::value)
    void load(T& any) {
        any.load(this);
    }

    void finish_save();

    void reset_load();

    // Returns false when failed
    bool write(std::ostream& os) const;

    // Returns false when failed
    bool read(std::istream& is);
};

struct UndoHistory {
    size_t undo_idx = 0;
    std::vector<SaveState> undo_history = {};

    // should be called after every edit
    template <class T>
    void push_undo_history(const T* obj) {
        assert(obj);

        if (this->undo_idx + 1 < this->undo_history.size()) {
            // Remove redos
            this->undo_history.resize(this->undo_idx + 1);
        }

        SaveState* new_save = &this->undo_history.emplace_back();
        new_save->save(*obj);
        new_save->finish_save();

        this->undo_idx = this->undo_history.size() - 1;
    }

    template <class T>
    void reset_undo_history(const T* obj) {
        // add initial undo
        this->undo_history.clear();
        this->undo_idx = 0;
        this->push_undo_history(obj);
    }

    constexpr bool can_undo() const {
        return this->undo_idx > 0;
    }

    template <class T>
    void undo(T* obj) {
        assert(this->can_undo());

        this->undo_idx--;
        this->undo_history[this->undo_idx].load(*obj);
        this->undo_history[this->undo_idx].reset_load();
    }

    constexpr bool can_redo() const {
        return this->undo_idx < this->undo_history.size() - 1;
    }

    template <class T>
    void redo(T* obj) {
        assert(this->can_redo());

        this->undo_idx++;
        this->undo_history[this->undo_idx].load(*obj);
        this->undo_history[this->undo_idx].reset_load();
    }

    UndoHistory() {}
    template <class T>
    UndoHistory(const T* obj) {
        reset_undo_history(obj);
    }

    UndoHistory(const UndoHistory&) = default;
    UndoHistory(UndoHistory&&) = default;
    UndoHistory& operator=(const UndoHistory&) = default;
    UndoHistory& operator=(UndoHistory&&) = default;
};
