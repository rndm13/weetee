#include "partial_dict.hpp"

// only include it for find_content_type
#include "httplib.h"

bool MultiPartBodyElementData::operator==(const MultiPartBodyElementData& other) const noexcept {
    return this->type != other.type && this->data != other.data;
}

void MultiPartBodyElementData::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->type);
    save->save(this->data);
    save->save(this->content_type);
}

bool MultiPartBodyElementData::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->type)) {
        return false;
    }
    if (!save->can_load(this->data)) {
        return false;
    }
    if (!save->can_load(this->content_type)) {
        return false;
    }

    return true;
}

void MultiPartBodyElementData::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->type);
    save->load(this->data);
    save->load(this->content_type);
}

void MultiPartBodyElementData::resolve_content_type() noexcept {
    std::visit(overloaded{
                   [this](const std::string& str) { this->content_type = "text/plain"; },
                   [this](const std::vector<std::string>& files) {
                       this->content_type = "";
                       for (const auto& file : files) {
                           std::string file_content_type =
                               httplib::detail::find_content_type(file, {}, "text/plain");
                           if (!this->content_type.empty()) {
                               this->content_type += ", ";
                           }
                           this->content_type += file_content_type;
                       }
                   },
               },
               this->data);
}

const char* MultiPartBodyElementData::field_labels[field_count] = {
    reinterpret_cast<const char*>("Type"),
    reinterpret_cast<const char*>("Data"),
    reinterpret_cast<const char*>("Content-Type"),
};

bool CookiesElementData::operator!=(const CookiesElementData& other) const noexcept {
    return this->data != other.data;
}

void CookiesElementData::save(SaveState* save) const noexcept {
    assert(save);

    save->save(data);
}

bool CookiesElementData::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(data)) {
        return false;
    }

    return true;
}

void CookiesElementData::load(SaveState* save) noexcept {
    assert(save);

    save->load(data);
}

const char* CookiesElementData::field_labels[field_count] = {
    reinterpret_cast<const char*>("Data"),
};

bool ParametersElementData::operator!=(const ParametersElementData& other) const noexcept {
    return this->data != other.data;
}

void ParametersElementData::save(SaveState* save) const noexcept {
    assert(save);

    save->save(data);
}

bool ParametersElementData::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(data)) {
        return false;
    }

    return true;
}

void ParametersElementData::load(SaveState* save) noexcept {
    assert(save);

    save->load(data);
}

const char* ParametersElementData::field_labels[field_count] = {
    reinterpret_cast<const char*>("Data"),
};

bool HeadersElementData::operator!=(const HeadersElementData& other) const noexcept {
    return this->data != other.data;
}

void HeadersElementData::save(SaveState* save) const noexcept {
    assert(save);

    save->save(data);
}

bool HeadersElementData::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(data)) {
        return false;
    }

    return true;
}

void HeadersElementData::load(SaveState* save) noexcept {
    assert(save);

    save->load(data);
}

const char* HeadersElementData::field_labels[field_count] = {
    reinterpret_cast<const char*>("Data"),
};

void VariablesElementData::save(SaveState* save) const noexcept {
    assert(save);

    save->save(data);
}

bool VariablesElementData::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(data)) {
        return false;
    }

    return true;
}

void VariablesElementData::load(SaveState* save) noexcept {
    assert(save);

    save->load(data);
}

const char* VariablesElementData::field_labels[field_count] = {
    reinterpret_cast<const char*>("Data"),
};
