#include "../../src/save_state.hpp"
#include "../../src/app_state.hpp"
#include "hello_imgui/runner_params.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    HelloImGui::RunnerParams params = {};
    AppState app(&params, true);
    SaveState ss = {};

    ss.save(reinterpret_cast<const char*>(Data), Size);
    ss.finish_save();

    if (ss.can_load(app) && ss.load_idx == ss.original_size) {
        ss.reset_load();
        ss.load(app);
    } else {
        return -1;
    }

    return 0;
}
