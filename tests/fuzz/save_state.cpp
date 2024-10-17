#include "../../src/save_state.hpp"
#include "../../src/app_state.hpp"
#include "hello_imgui/runner_params.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    HelloImGui::RunnerParams params = {};
    AppState app(&params);
    SaveState ss = {};

    ss.save(reinterpret_cast<const char*>(Data), Size);
    ss.finish_save();

    if (ss.can_load(app)) {
        ss.reset_load();
        ss.load(app);
    } else {
        return -1;
    }

    return 0;
}
