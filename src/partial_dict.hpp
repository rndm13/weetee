#pragma once

#include "i18n.hpp"
#include "save_state_incl.hpp"
#include "textinputcombo.hpp"

#include "cmath"
#include "optional"
#include "string"
#include "vector"

enum PartialDictElementFlags : uint8_t {
    PARTIAL_DICT_ELEM_ENABLED = 1 << 0,

    PARTIAL_DICT_ELEM_SELECTED = 1 << 1,
    PARTIAL_DICT_ELEM_TO_DELETE = 1 << 2,

    // No key change and no delete/disable
    PARTIAL_DICT_ELEM_REQUIRED = 1 << 3,
};

template <typename Data> struct PartialDictElement {
    uint8_t flags = PARTIAL_DICT_ELEM_ENABLED;

    // save
    std::string key;
    Data data;

    // do not save
    std::optional<ComboFilterState> cfs; // If hints are given

    SAVE_STATE_SAVE_IMPL(flags, 0, key, 0, data, 0);

    bool operator!=(const PartialDictElement<Data>& other) const {
        return this->flags != other.flags || this->data != other.data;
    }
};

template <typename Data> struct PartialDict {
    using DataType = Data;
    using ElementType = PartialDictElement<Data>;
    using SizeType = typename std::vector<PartialDictElement<Data>>::size_type;
    std::vector<PartialDictElement<Data>> elements;

    // Do not save
    PartialDictElement<Data> add_element;
    size_t last_selected_idx = 0;

    bool operator==(const PartialDict& other) const {
        return this->elements == other.elements;
    }

    SAVE_STATE_SAVE_IMPL(elements, 0);

    constexpr bool empty() const { return this->elements.empty(); }
};

enum PartialDictFlags : uint8_t {
    PARTIAL_DICT_NONE = 0,
    PARTIAL_DICT_NO_DELETE = 1 << 0,
    PARTIAL_DICT_NO_CREATE = 1 << 1,
    PARTIAL_DICT_NO_ENABLE = 1 << 2,
    PARTIAL_DICT_NO_KEY_CHANGE = 1 << 3,
};

enum MultiPartBodyDataType : uint8_t {
    MPBD_FILES,
    MPBD_TEXT,
};

// static const char* MPBDTypeLabels[] = {
//     /* [MPBD_FILES] = */ reinterpret_cast<const char*>("Files"),
//     /* [MPBD_TEXT] = */ reinterpret_cast<const char*>("Text"),
// };

using MultiPartBodyData = std::variant<std::vector<std::string>, std::string>;
struct MultiPartBodyElementData {
    MultiPartBodyDataType type;
    MultiPartBodyData data;
    std::string content_type;

    static constexpr size_t field_count = 3;
    static std::array<const char*, field_count> field_labels(const I18N* i18n);

    bool operator==(const MultiPartBodyElementData& other) const;

    SAVE_STATE_SAVE_IMPL(type, 0, data, 0, content_type, 0);

    void save(SaveState* save) const;
    bool can_load(SaveState* save) const;
    void load(SaveState* save);

    void resolve_content_type();
};

using MultiPartBody = PartialDict<MultiPartBodyElementData>;
using MultiPartBodyElement = MultiPartBody::ElementType;

struct CookiesElementData {
    std::string data;

    static constexpr size_t field_count = 1;
    static std::array<const char*, field_count> field_labels(const I18N* i18n);

    bool operator!=(const CookiesElementData& other) const;

    SAVE_STATE_SAVE_IMPL(data, 0);
};
using Cookies = PartialDict<CookiesElementData>;
using CookiesElement = Cookies::ElementType;

struct ParametersElementData {
    std::string data;

    static constexpr size_t field_count = 1;
    static std::array<const char*, field_count> field_labels(const I18N* i18n);
    // static const char* field_labels[field_count];

    bool operator!=(const ParametersElementData& other) const;

    SAVE_STATE_SAVE_IMPL(data, 0);
};
using Parameters = PartialDict<ParametersElementData>;
using ParametersElement = Parameters::ElementType;

struct HeadersElementData {
    std::string data;

    static constexpr size_t field_count = 1;
    static std::array<const char*, field_count> field_labels(const I18N* i18n);

    bool operator!=(const HeadersElementData& other) const;

    SAVE_STATE_SAVE_IMPL(data, 0);
};
using Headers = PartialDict<HeadersElementData>;
using HeadersElement = Headers::ElementType;

struct VariablesElementData {
    std::optional<char> separator;
    std::string data;

    static constexpr size_t field_count = 1;
    static std::array<const char*, field_count> field_labels(const I18N* i18n);

    bool operator!=(const VariablesElementData& other) const;

    SAVE_STATE_SAVE_IMPL(data, 0, separator, 0);
};

using Variables = PartialDict<VariablesElementData>;
using VariablesElement = Variables::ElementType;
