#include "app/app.h"

void TW_Pad(int port, input::Pad& pad);  // src/game/tw_test.cpp
bool TW_Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);  // src/game/tw_audio.cpp
void TW_Ui(double fps);
void TW_Window(void* hwnd);  // src/game/tw_widescreen.cpp

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
    .options = {},                        // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = nullptr,                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = TW_Window,                  // the game window is up
    .message = TW_Message,                // window messages first; true takes one
    .ui = TW_Ui,                          // ImGui drawing each frame
    .pad = TW_Pad,                        // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
};
