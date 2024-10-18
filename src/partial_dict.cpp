#include "partial_dict.hpp"

bool MultiPartBodyElementData::operator==(const MultiPartBodyElementData& other) const {
    return this->type != other.type && this->data != other.data;
}

void MultiPartBodyElementData::resolve_content_type() {
    switch (this->type) {
    case MPBD_FILES: {
        assert(std::holds_alternative<std::vector<std::string>>(this->data));
        auto& files = std::get<std::vector<std::string>>(this->data);

        this->content_type = "";
        for (const auto& file : files) {
            std::string file_content_type =
                httplib::detail::find_content_type(file, {}, "text/plain");
            if (!this->content_type.empty()) {
                this->content_type += ", ";
            }
            this->content_type += file_content_type;
        }
    } break;
    case MPBD_TEXT: {
        this->content_type = "text/plain";
    } break;
    }
}

std::array<const char*, MultiPartBodyElementData::field_count>
MultiPartBodyElementData::field_labels(const I18N* i18n) {
    return {
        i18n->ed_mpbd_type.c_str(),
        i18n->ed_mpbd_data.c_str(),
        i18n->ed_mpbd_content_type.c_str(),
    };
}

bool CookiesElementData::operator!=(const CookiesElementData& other) const {
    return this->data != other.data;
}

std::array<const char*, CookiesElementData::field_count>
CookiesElementData::field_labels(const I18N* i18n) {
    return {
        i18n->ed_pd_data.c_str(),
    };
}

bool ParametersElementData::operator!=(const ParametersElementData& other) const {
    return this->data != other.data;
}

std::array<const char*, ParametersElementData::field_count>
ParametersElementData::field_labels(const I18N* i18n) {
    return {
        i18n->ed_pd_data.c_str(),
    };
}

bool HeadersElementData::operator!=(const HeadersElementData& other) const {
    return this->data != other.data;
}

std::array<const char*, HeadersElementData::field_count>
HeadersElementData::field_labels(const I18N* i18n) {
    return {
        i18n->ed_pd_data.c_str(),
    };
}

bool VariablesElementData::operator!=(const VariablesElementData& other) const {
    return this->data != other.data;
}

std::array<const char*, VariablesElementData::field_count>
VariablesElementData::field_labels(const I18N* i18n) {
    return {
        i18n->ed_pd_data.c_str(),
    };
}
