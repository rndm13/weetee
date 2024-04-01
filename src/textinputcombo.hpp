#pragma once

#include "string"

struct ComboFilterState {
    int activeIdx;         // Index of currently 'active' item by use of up/down keys
    bool selectionChanged; // Flag to help focus the correct item when selecting active item
    bool textChanged;      // Flag to help with starting popup
    bool historyMove;      // Flag to help with moving history via arrows
};

bool ComboFilter(const char* id, std::string* str, const char** hints, const size_t hint_count, ComboFilterState* s);
