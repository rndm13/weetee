#include "../../src/save_state.hpp"
#include "../../src/app_state.hpp"
#include "hello_imgui/runner_params.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    HelloImGui::RunnerParams params = {};
    AppState app(&params, true);
    SaveState save = {};

    save.save(reinterpret_cast<const char*>(Data), Size);
    save.finish_save();
    save.reset_load();

    if (!save.load(app) || save.load_idx != save.original_size) {
        return -1;
    }

    return 0;
}
