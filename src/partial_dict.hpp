#pragma once

#include "save_state.hpp"
#include "textinputcombo.hpp"

#include "portable_file_dialogs/portable_file_dialogs.h"

#include "cmath"
#include "optional"
#include "string"
#include "vector"

template <typename Data> struct PartialDictElement {
    bool enabled = true;

    // do not save
    bool selected = false;
    bool to_delete = false;

    // save
    std::string key;
    Data data;

    // do not save
    std::optional<ComboFilterState> cfs; // If hints are given

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(this->enabled);
        save->save(this->key);
        save->save(this->data);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(this->enabled)) {
            return false;
        }
        if (!save->can_load(this->key)) {
            return false;
        }
        if (!save->can_load(this->data)) {
            return false;
        }

        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(this->enabled);
        save->load(this->key);
        save->load(this->data);
    }

    bool operator!=(const PartialDictElement<Data>& other) const noexcept {
        return this->data != other.data;
    }
};

template <typename Data> struct PartialDict {
    using DataType = Data;
    using ElementType = PartialDictElement<Data>;
    using SizeType = std::vector<PartialDictElement<Data>>::size_type;
    std::vector<PartialDictElement<Data>> elements;

    // Do not save
    PartialDictElement<Data> add_element;

    bool operator==(const PartialDict& other) const noexcept {
        return this->elements == other.elements;
    }

    void save(SaveState* save) const noexcept {
        assert(save);
        save->save(this->elements);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);
        return save->can_load(this->elements);
    }

    void load(SaveState* save) noexcept { 
        assert(save);
        return save->load(this->elements);
    }

    constexpr bool empty() const noexcept { return this->elements.empty(); }
};

enum PartialDictFlags {
    PARTIAL_DICT_NONE = 0,
    PARTIAL_DICT_NO_DELETE = 1 << 0,
    PARTIAL_DICT_NO_CREATE = 1 << 1,
    PARTIAL_DICT_NO_ENABLE = 1 << 2,
};

enum MultiPartBodyDataType : uint8_t {
    MPBD_FILES,
    MPBD_TEXT,
};
static const char* MPBDTypeLabels[] = {
    /* [MPBD_FILES] = */ reinterpret_cast<const char*>("Files"),
    /* [MPBD_TEXT] = */ reinterpret_cast<const char*>("Text"),
};
using MultiPartBodyData = std::variant<std::vector<std::string>, std::string>;
struct MultiPartBodyElementData {
    MultiPartBodyDataType type;
    MultiPartBodyData data;

    // do not save
    std::optional<pfd::open_file> open_file;

    static constexpr size_t field_count = 2;
    static const char* field_labels[field_count];

    bool operator==(const MultiPartBodyElementData& other) const noexcept;

    void save(SaveState* save) const noexcept;

    bool can_load(SaveState* save) const noexcept;

    void load(SaveState* save) noexcept;
};
using MultiPartBody = PartialDict<MultiPartBodyElementData>;
using MultiPartBodyElement = MultiPartBody::ElementType;

struct CookiesElementData {
    std::string data;

    static constexpr size_t field_count = 1;
    static const char* field_labels[field_count];

    bool operator!=(const CookiesElementData& other) const noexcept;

    void save(SaveState* save) const noexcept;

    bool can_load(SaveState* save) const noexcept;

    void load(SaveState* save) noexcept;
};
using Cookies = PartialDict<CookiesElementData>;
using CookiesElement = Cookies::ElementType;

struct ParametersElementData {
    std::string data;

    static constexpr size_t field_count = 1;
    static const char* field_labels[field_count];

    bool operator!=(const ParametersElementData& other) const noexcept;

    void save(SaveState* save) const noexcept;

    bool can_load(SaveState* save) const noexcept;

    void load(SaveState* save) noexcept;
};
using Parameters = PartialDict<ParametersElementData>;
using ParametersElement = Parameters::ElementType;

struct HeadersElementData {
    std::string data;

    static constexpr size_t field_count = 1;
    static const char* field_labels[field_count];

    bool operator!=(const HeadersElementData& other) const noexcept;

    void save(SaveState* save) const noexcept;

    bool can_load(SaveState* save) const noexcept;

    void load(SaveState* save) noexcept;
};
using Headers = PartialDict<HeadersElementData>;
using HeadersElement = Headers::ElementType;
