# Tiger Woods PGA Tour 2004 - XBE Recomp

An unofficial native PC port of the original Xbox version of Tiger Woods PGA Tour 2004, with extended graphics options, widescreen, PC controls and other enhancements.

This port was created using the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool.

> [!NOTE]
> This project does not include any game code or assets. You must provide the files from your own legally acquired copy of the game. The first start builds the game's code on your PC from that copy.

> [!IMPORTANT]
> Only the **US (NTSC) Xbox version** of the game is supported. Other releases have a different `default.xbe`, and the port tells you if it finds one. The supported `default.xbe` has the SHA-1 `64a4e6978026185a537e1860b755652b976f6559`.

## Installation

- Extract the release, keeping its folders together.
- Put your game files in the `assets` folder. This can be an ISO, or the extracted data files.
- Run `TW2004.exe`. The first start compiles the game's code for your PC. This only happens once, and it needs about 1 GB of free space while it works.
- Settings can be adjusted in the quick menu, or in `settings.toml` beside the exe.

## Features

### Quick Menu
Press **F1**, or **Back + Start** on a controller, to open it over the game. Changes apply at once and are saved.

- **Display** - Window mode, resolution scaling (classic, desktop, 2x and 3x), anti-aliasing (SMAA or 2x MSAA), texture filtering up to 16x, V-Sync, button prompts and full detail trees.
- **Effects** - Bloom and depth of field.
- **Controls** - Controller and keyboard and mouse diagrams, matching what you last used.

### Display
- **Widescreen** - The 3D picture fills the screen, with the HUD and menus laid out for the wider frame.
- **Splitscreen** - Split views fill the screen.
- **Frame pacing** - Frames follow a steady timer, off the game's own processor core.

### Graphics
- **Effects** - Bloom and depth of field, each of which can be turned off. Bloom is off in splitscreen.
- **Full detail trees** - Trees keep their full model at any distance, without level of detail changes.
- **Fixes** - Corrected distance fog, the ball's flight trail, the putting line and the shot preview.

### Controls
Supports Xbox, PlayStation and Switch controllers, with hot plugging and rumble, or keyboard and mouse.

| Action | Keyboard and mouse |
|---|---|
| Swing | Hold left click, drag down, then drag up |
| Confirm | Enter or Space |
| Back | Backspace |
| Pause | Esc or P |
| Quick menu | F1 |
| Menus | Arrow keys or WASD |

The rest are shown on the quick menu's Controls page, and keys can be changed in `settings.toml`.

### Button Prompts
Prompts are redrawn at higher resolution for Xbox and PlayStation controllers and for keyboard and mouse, and follow whatever you last used. The Button Prompts option can fix one set.

## System Requirements

### Minimum
- **OS**
  - Windows 10 64-bit
- **GPU** with support for Direct3D 12

## Known Issues

- The mouse swing has a short delay as you start to drag.
- Left click moves the selection in the game's menus while held.

## Building

### 1. Clone the Repository
Clone this repository using [Git](https://git-scm.com/).

### 2. Add the Required Game Files
Place your extracted game data files in the `assets` folder. `default.xbe` must be there, with the game's folders beside it.

### 3. Install Dependencies
Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two).

### 4. Build the Project
Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`.

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this is built on.
- [Athelas](https://github.com/Shagg88/Athelas), whose work on the same recompiler this port's SDK builds on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the menus and [SDL](https://github.com/libsdl-org/SDL) for controllers.
- [LLVM](https://llvm.org)'s Clang and LLD, which build the game's code on your PC.
- [SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez and others for the SMAA anti-aliasing option.
- [Archivo Black](https://github.com/Omnibus-Type/ArchivoBlack) (SIL Open Font License) for the quick menu.
- [xemu](https://github.com/xemu-project/xemu)'s DSP56300 interpreter and DSP DMA unit, which run the game's own audio effects (`sdk/third_party/xemu-dsp`).
- The controller diagram's proportions follow TheHoodieGuy02's [8th Gen Console Vector Gamepad Collection](https://opengameart.org/content/8th-gen-console-vector-gamepad-collection) (CC0).

Tiger Woods PGA Tour is a trademark of its owners. This project is unofficial and is not affiliated with or endorsed by them. Controller button names and symbols belong to their owners and are used only to show which button to press.

## License

This port is free software under the [GNU General Public License, version 3](LICENSE). It includes xemu's DSP code (GPL-2.0-or-later and LGPL-2.1-or-later, see `sdk/third_party/xemu-dsp`), so builds that contain it are distributed under the GPL. The SDK in `sdk/` keeps its own MIT licence (`sdk/LICENSE`) for the parts that come from it. No game code or data is included: you supply your own copy of the game.
