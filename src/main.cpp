#include "hello_imgui/runner_params.h"
#include "hello_imgui/internal/hello_imgui_ini_settings.h"

#include "immapp/immapp.h"

#include "gui.hpp"
#include "gui_tests.hpp"
#include "app_state.hpp"

// TODOOO: fix a crash when opening request parameters when response body is opened
// TODO: Swagger file import/export (medium)
// TODO: Implement file sending (medium)
// TODO: Implement variables for groups with substitution (hard)
// TODO: add fuzz tests (very very hard)
// TODO: Fix can_load... (bullshit)
// TODO: make dynamic tests work (and with keep alive connection) (very hard)
// TODO: add authentication (I have no idea)
// TODO: add move reordering (very hard)

void post_init(AppState* app) noexcept;

int main() {
    HelloImGui::RunnerParams runner_params;
    auto app = AppState(&runner_params);

    runner_params.appWindowParams.windowTitle = "weetee";

    runner_params.imGuiWindowParams.showMenuBar = true;
    runner_params.imGuiWindowParams.showStatusBar = true;
    runner_params.imGuiWindowParams.rememberTheme = true;

    runner_params.imGuiWindowParams.defaultImGuiWindowType =
        HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;

    runner_params.callbacks.ShowGui = [&app]() { show_gui(&app); };
    runner_params.callbacks.ShowMenus = [&app]() { show_menus(&app); };
    runner_params.callbacks.LoadAdditionalFonts = [&app]() { load_fonts(&app); };
    runner_params.callbacks.PostInit = [&app]() { post_init(&app); };
    runner_params.callbacks.RegisterTests = [&app]() { register_tests(&app); };

    runner_params.dockingParams = layout(&app);
    runner_params.fpsIdling.enableIdling = false;
    runner_params.useImGuiTestEngine = true;

    ImmApp::AddOnsParams addOnsParams;
    ImmApp::Run(runner_params, addOnsParams);
    return 0;
}

// Needs to be in this file for compile definitions
void post_init(AppState* app) noexcept {
    std::string ini = HelloImGui::IniSettingsLocation(*app->runner_params);
    Log(LogLevel::Debug, "Ini: %s", ini.c_str());
    HelloImGui::HelloImGuiIniSettings::LoadHelloImGuiMiscSettings(ini, app->runner_params);
    Log(LogLevel::Debug, "Theme: %s",
        ImGuiTheme::ImGuiTheme_Name(app->runner_params->imGuiWindowParams.tweakedTheme.Theme));

#if !CPPHTTPLIB_OPENSSL_SUPPORT
    Log(LogLevel::Warning, "Compiled without OpenSSL support! HTTPS will not work!");
#endif
#if !CPPHTTPLIB_ZLIB_SUPPORT
    Log(LogLevel::Warning, "Compiled without ZLib support! Zlib compression will not work!");
#endif
#if !CPPHTTPLIB_BROTLI_SUPPORT
    Log(LogLevel::Warning, "Compiled without Brotli support! Brotli compression will not work!");
#endif

    // NOTE: you have to do this in show_gui instead because imgui is stupid
    // ImGuiTheme::ApplyTweakedTheme(app->runner_params->imGuiWindowParams.tweakedTheme);
}
