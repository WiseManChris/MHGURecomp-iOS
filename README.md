# MHGU Recomp (iOS & iPadOS)

[![Build iOS IPA](https://github.com/WiseManChris/MHGURecomp-iOS/actions/workflows/build-ios.yml/badge.svg)](https://github.com/WiseManChris/MHGURecomp-iOS/actions/workflows/build-ios.yml)
[![Platform: iOS / iPadOS](https://img.shields.io/badge/Platform-iOS%20%7C%20iPadOS%2015.0%2B-blue.svg)](https://github.com/WiseManChris/MHGURecomp-iOS)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.txt)

A native iOS and iPadOS port of **Monster Hunter Generations Ultimate** (Nintendo Switch, version 1.4.0), running via static recompilation. 

The game's 32-bit ARM machine code is recompiled ahead-of-time directly into native 64-bit ARM (ARM64) host instructions, running with native Metal hardware acceleration via MoltenVK (Vulkan 1.2 on Metal 3).

> **[!IMPORTANT]**
> **No copyrighted game files are included.** You must provide your own legally dumped game files (`exefs` and `romfs`) from a Nintendo Switch console or emulator. This project does not contain or distribute proprietary Nintendo or Capcom assets.

---

## Highlights & Features

- **No JIT Required**: Because the game is statically recompiled ahead-of-time into native code, **no JIT compiler, developer mode debugging, or tethering is needed**. Works out of the box with AltStore, Sideloadly, TrollStore, and LiveContainer.
- **Metal Graphics Acceleration**: Built on MoltenVK (Vulkan 1.2 translating directly to Apple Metal 3).
- **Smooth 60 FPS**: Locked 60 frames per second at Full HD (1920x1080) and Retina display resolutions.
- **Full Controller Support**: Automatic plug-and-play support for Nintendo Switch Pro Controllers, Joy-Cons, PS4 DualShock 4, PS5 DualSense, Xbox Wireless Controllers, Backbone One, and other MFi / Bluetooth controllers via SDL3.
- **Magic Keyboard & Smart Keyboard**: Full hardware keyboard navigation, character naming, and text input with automatic on-screen keyboard suppression when navigating in-game menus.
- **Embedded Automatic Mod Extraction**:
  - Automatically unzips and installs **`.zip`** archives placed into the `mods/` folder.
  - Features an **integrated ANSI-C LZMA SDK 7-Zip decoder**: seamlessly extracts multi-hundred-megabyte `.7z` mod archives (such as the community **MHGU HD Project** with 7,600+ textures) directly on device without requiring a PC or third-party unarchiver apps.
- **Integrated Forge Mod Loader & In-Game GUI**:
  - Full support for Forge `.nro` code plugins (`damage_numbers.nro`, `key_quest_markers.nro`, etc.).
  - Built-in Dear ImGui overlay displaying loaded plugins, real-time damage metrics, and active RomFS asset mods with file counts.
- **Cross-Save Compatible**: Seamlessly transfer your save files (`system`, `system_backup`) directly from your Nintendo Switch console, Ryujinx, or PC recompilation into the app.

---

## Requirements

- **Device**: Any 64-bit Apple Silicon device running **iOS 15.0+** or **iPadOS 15.0+**:
  - iPad Pro, iPad Air, iPad mini (M1, M2, M4, A14, A15, A16, A17 Pro, etc.)
  - iPhone (iPhone 11 through iPhone 16 Pro Max)
- **Sideloading Utility**: AltStore, Sideloadly, TrollStore, LiveContainer, or Xcode / iOS App Signer.
- **Game Files**: Monster Hunter Generations Ultimate, update version 1.4.0 (Title ID `0100770008DD8000`).

---

## Installation & Setup Guide

### 1. Sideload the App
1. Download the latest `MHGURecomp.ipa` from the [Releases](https://github.com/WiseManChris/MHGURecomp-iOS/releases) page.
2. Install the IPA onto your iPhone or iPad using your preferred sideloading tool (AltStore, Sideloadly, TrollStore, etc.).
3. Tap the game icon once on your home screen to launch it and initialize the app's directory structure in the iOS **Files** app. Close the app once it prompts for game files.

### 2. Copy Game Files via the Files App
Open the **Files** app on your iOS / iPadOS device and navigate to:
```text
On My iPad (or On My iPhone) > MHGURecomp > data
```

Place your dumped game files into `data` following this exact folder structure:
```text
MHGURecomp/
  data/
    exefs/
      main
      main.npdm
      rtld
      sdk
      subsdk0
    romfs/
      nativeNX/
        arc/
        eng/
        loc/
        movie/
        sound/
        system/
        ... (rest of the game's romfs contents)
```

> **[!NOTE]**
> - The `exefs` files must be from the **v1.4.0** title update.
> - The `romfs` folder should be the base game's RomFS with the v1.4.0 update merged over it (standard Switch dump format).

---

## Mods & Plugins

The iOS port features an advanced RomFS virtual layer system and an automatic mod archive extractor.

### Installing Texture & Asset Mods (e.g. MHGU HD Project)
1. In the **Files** app, open `MHGURecomp > mods`.
2. Simply copy your `.zip` or `.7z` mod archives into the `mods` folder:
   - For the **MHGU HD Project**, drop `Main.7z` and `Monsters.7z` directly into `mods/`.
3. Launch the game! On initial boot, the game will automatically decompress the archives into dedicated subfolders (`mods/Main/`, `mods/Monsters/`) and rename the archives to `.7z.installed`.
4. All high-definition textures, vector UI assets, and models are mounted and active immediately.

### Installing Forge Plugins (.nro)
- Forge code mods (such as **Damage Numbers** or **Key Quest Markers**) can be dropped as `.zip` archives or unpacked folders into `mods/`.
- The port automatically detects plugins inside `romfs/nativeNX/plugins/*.nro` and links them to the game's interpreter and memory space.

### In-Game Forge Overlay Menu
- **Opening the menu**: Press `Insert` on a hardware keyboard, or press the configured toggle combination.
- **Features in the overlay**:
  - **Active Mods (RomFS / Textures)**: Shows all active RomFS layers (e.g. `MHGU HD Project (Main): 7630 files active`, `MHGU HD Project (Monsters): 35 files active`) and total virtual image size.
  - **Loaded Plugins (.nro)**: View status and configuration toggles for loaded `.nro` plugins.
  - **Plugin UI**: Customize damage numbers colors, font sizes, display positions, and key quest tracking in real-time.

---

## Save Data Transfer

Save data is kept in:
```text
On My iPad (or On My iPhone) > MHGURecomp > save > save_0
```

To transfer an existing save from a modded Nintendo Switch, Ryujinx, or PC Recomp:
1. Copy your `system` and `system_backup` save files.
2. Paste them into `save/save_0/` in the `MHGURecomp` directory.
3. Launch the game—your hunters, gear, and progress will be right there.

---

## Settings & Configuration (`mhgurecomp.ini`)

On first launch, the game generates `mhgurecomp.ini` in the app's root folder. You can edit this file in the Files app or a text editor to fine-tune settings:

- **Resolution & Aspect Ratio**:
  - `Mode = Borderless`
  - `Resolution = 1920x1080` (or set higher for Retina displays)
  - `AspectRatio = Auto` (fills widescreen and 4:3 iPad displays cleanly with centered HUD)
- **Graphics & Performance**:
  - `FPS = 60` (or 120 for ProMotion displays)
  - `VSync = On`
  - `MSAA = 1x` (or 2x / 4x)
  - `FXAA = On`
- **Multiplayer & Quest Options**:
  - `VillageQuestsInHub = On`: Post and play single-player Village quests at the Hub quest counter!
  - `HubHP = 2.00`: Rebalanced health multiplier for village quests taken in multiplayer.

---

## Controls

| Switch Button | Gamepad (Controller) | Hardware Keyboard |
| --- | --- | --- |
| **Left Stick** | Left Stick | `W` `A` `S` `D` (Hold `LShift` to walk) |
| **Right Stick** | Right Stick | Mouse camera / Arrow keys |
| **A / B / X / Y** | Face Buttons | Left Click (`X`), Right Click (`A`), `Space` (`B`), `R` (`Y`) |
| **L / R** | Bumpers | `Q` / `E` |
| **ZL / ZR** | Triggers | `Tab` / `T` |
| **Plus / Minus** | Start / Select | `Enter` / `Backspace` |
| **D-Pad** | D-Pad | Arrow keys |
| **Forge Overlay**| - | `Insert` |

---

## Building from Source

The iOS version uses a tailored build pipeline using CMake and Apple Clang.

### Prerequisites (macOS)
- macOS with Xcode 15+ and iOS SDK
- CMake 3.25+ and Ninja
- Python 3.10+
- Dependencies: MoltenVK, SDL3 iOS framework, Shaderc

### Build Command
```bash
# Clone the repository recursively
git clone --recursive https://github.com/WiseManChris/MHGURecomp-iOS.git
cd MHGURecomp-iOS

# Configure and compile for iOS
cmake -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="YOUR_TEAM_ID"
cmake --build build-ios --config Release
```
Automated build artifacts (`.ipa`) are also compiled via GitHub Actions on every push to `master`.

---

## Credits & Acknowledgments

This project would not be possible without the incredible work of the following developers and open-source projects:

- **[morerokk](https://github.com/morerokk/MHGURecomp)**: Creator of the original MHGU Recomp project, static recompilation tools, ARM lifter, and runtime architecture.
- **[WiseManChris](https://github.com/WiseManChris)**: iOS and iPadOS port development, MoltenVK pipeline integration, memory model tuning, in-app `.7z` and `.zip` mod archive extractors, and software keyboard handling.
- **[Fexty](https://github.com/Fexty12573)** & **jeffi2287**: Creators of the **Forge** mod loader and API, enabling guest code plugins and hooks.
- **[Monkbreh](https://github.com/Monkbreh/MHGU-HD-Project)**: Creator of the **MHGU HD Project**, providing the comprehensive ESRGAN remastered textures and assets.
- **PurplefinNeptuna**: Creator of the indispensable **Key Quest Markers** mod.
- **Igor Pavlov & The 7-Zip Team**: Authors of the ANSI-C LZMA SDK decoder utilized for embedded `.7z` archive extraction.
- **The Khronos Group & The MoltenVK Contributors**: For the Vulkan-to-Metal translation layer powering graphics on iOS.
- **libsdl-org**: For **SDL3**, providing robust iOS windowing, Metal view integration, and gamepad input.
- **Capcom & Nintendo**: For creating *Monster Hunter Generations Ultimate* (*Monster Hunter XX*).

---

## Legal & Disclaimer

- Monster Hunter Generations Ultimate is a registered trademark of Capcom Co., Ltd.
- Nintendo Switch is a registered trademark of Nintendo Co., Ltd.
- This software is an independent static recompiler and runtime. It does not contain any proprietary code, cryptographic keys, or assets from Nintendo or Capcom.
- Users must dump their own legally purchased copy of the game to use this software.
