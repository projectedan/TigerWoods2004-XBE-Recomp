#include "app/app.h"
#include "core/keys.h"

void TW_Pad(int port, input::Pad& pad);  // src/game/tw_test.cpp
bool TW_Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);  // src/game/tw_audio.cpp
void TW_Ui(double fps);
void TW_Window(void* hwnd);  // src/game/tw_widescreen.cpp
void TW_MenuToggle();              // src/game/tw_menu.cpp
bool TW_MenuOpen();
void TW_MenuFonts();
void TW_FileOpened(const char* name);  // src/game/tw_text.cpp
void TW_TextStart();
extern bool g_tw_bloom, g_tw_focus, g_tw_ao, g_tw_full_detail;
extern float g_tw_bloom_amount;
extern uint32_t g_tw_prompts;

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Tiger Woods PGA Tour 2004",             // window and splash
    .short_title = L"TW2004",              // message boxes
    .name = L"TW2004",                     // window classes, crash dumps, handover event
    .log = "tw2004",                       // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "TW2004"}},  // each recompiled xbe and its exe, for title launches
    // game settings kept in the settings file; the menu is tw_menu.cpp's
    .options = {{"effects", "bloom", cfg::Type::Bool, "Bloom", "A soft glow around the sky and bright areas.", &g_tw_bloom},
                {"effects", "bloom_intensity", cfg::Type::Float, "Bloom intensity", "", nullptr, nullptr, nullptr, 0, 100, {}, nullptr, &g_tw_bloom_amount},
                {"effects", "depth_of_field", cfg::Type::Bool, "Depth of field", "The far distance softens.", &g_tw_focus},
                {"effects", "ambient_occlusion", cfg::Type::Bool, "Ambient occlusion", "Soft shade where things meet the ground and each other.", &g_tw_ao},
                {"input", "button_prompts", cfg::Type::Choice, "Button prompts", "The buttons shown on screen: those of what you last used, or a set you choose.", nullptr, &g_tw_prompts, nullptr, 0, 0, {"auto", "xbox", "playstation", "keyboard"}, nullptr, nullptr, {"Auto", "Xbox", "PlayStation", "Keyboard and mouse"}},
                {"display", "classic_resolution", cfg::Type::Bool, "Classic resolution", "The frame drawn at the console's own 480 lines.", &app::g_render_classic},
                {"effects", "full_detail", cfg::Type::Bool, "Full detail trees", "Trees keep their full model at any distance.", &g_tw_full_detail}},
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = nullptr,                      // after the settings load, before the window and boot
    .loaded = TW_TextStart,               // image in memory, before its entry point runs
    .window = TW_Window,                  // the game window is up
    .message = TW_Message,                // window messages first; true takes one
    .ui = TW_Ui,                          // ImGui drawing each frame
    .pad = TW_Pad,                        // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
    .key_defaults = {{keys::kA, "Enter, Space"}, {keys::kB, "Backspace"}, {keys::kStart, "Esc, P"}, {keys::kRightTrigger, "Mouse3"}},
    .mouse_swing = true,                  // hold the left mouse button to swing
    .menu_toggle = TW_MenuToggle,
    .menu_open = TW_MenuOpen,
    .fonts = TW_MenuFonts,
    .file_opened = TW_FileOpened,
};
