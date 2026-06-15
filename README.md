# Floating Damage Informer

Floating Damage Informer is a native SKSE C++ plugin for Skyrim that displays floating damage numbers above actors damaged in game. The built plugin file is named `FloatingDamageNumbersNG.dll`.

![Floating Damage Informer in-game example](assets/screenshots/floating-damage-informer-example.jpg)

In-game example showing floating damage numbers during combat. Screenshot content from Skyrim is shown for illustrative purposes only.

The plugin is designed to stay lightweight:

- no Papyrus scripts
- no ESP, ESM, or ESL plugin file
- no MCM or SkyUI requirement
- no bundled SWF, mesh, texture, or font assets
- no HUD file replacement

Rendering currently uses dynamic Scaleform `TextField` objects inside the existing HUD movie. The plugin projects actor anchors from the live game world to the HUD and fails safely if the HUD movie cannot create dynamic text.

## Runtime Support

The project is built with the CommonLibSSE NG-compatible `commonlibsse-ng-fork` vcpkg package and is intended for:

- Skyrim SE 1.5.97
- Skyrim AE 1.6.x, including GOG AE where SKSE and CommonLib support it
- Skyrim VR 1.4.15

Actual runtime compatibility still depends on matching SKSE, Address Library, and CommonLib package support for your game version.

## Requirements

### For Players

- Skyrim Special Edition, Anniversary Edition, GOG Anniversary Edition, or Skyrim VR
- The matching Skyrim Script Extender build for your runtime
- Address Library for SKSE Plugins, or VR Address Library for SKSEVR

### For Building From Source

- Windows
- Git
- CMake 3.28 or newer
- Visual Studio 2022, or Visual Studio 2026 if you have the newer generator installed
- vcpkg with `VCPKG_ROOT` set
- A vcpkg triplet compatible with the project presets, currently `x64-windows-static-md`

The full SKSE plugin build uses vcpkg and CommonLibSSE. The core unit tests can be built without the SKSE/CommonLib dependency graph.

## Installation

Copy the runtime files into your Skyrim data directory:

```text
Data/
  SKSE/
    Plugins/
      FloatingDamageNumbersNG.dll
      FloatingDamageNumbersNG.ini
```

If you build from source, CMake copies both files to:

```text
build/<preset>/package/SKSE/Plugins/
```

You can copy the `SKSE` folder from that package directory into your game's `Data` directory.

## Configuration

The default configuration file is:

```text
package/SKSE/Plugins/FloatingDamageNumbersNG.ini
```

At runtime it belongs in:

```text
Data/SKSE/Plugins/FloatingDamageNumbersNG.ini
```

Important options include:

- `bEnabled`: master on/off switch
- `bShowPlayerDealtDamage`: show damage dealt by the player
- `bShowFollowerDealtDamage`: include follower damage
- `bShowNPCvsNPCDamage`: include NPC-vs-NPC damage
- `bShowDamageTakenByPlayer`: show damage taken by the player
- `fMinDamageToShow`: minimum displayed damage value
- `fMergeWindowMs`: window used to merge rapid damage ticks
- `fLifetime`: how long a number remains visible
- `fMaxDistance`: maximum display distance
- `iMaxActiveNumbers`: global cap for active HUD numbers
- `iMaxNumbersPerActor`: per-actor cap for active HUD numbers
- `sPreferredRenderer`: currently expected to be `Scaleform`

Logs are written to the normal SKSE log directory as:

```text
FloatingDamageNumbersNG.log
```

## Build Setup

Clone the repository:

```powershell
git clone https://github.com/<owner>/Floating-Damage-Informer.git
cd Floating-Damage-Informer
```

Install and bootstrap vcpkg if you do not already have it:

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\vcpkg"
```

For a permanent PowerShell user environment variable:

```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
```

Open a new terminal after setting the permanent variable.

## Building The SKSE Plugin

Visual Studio 2022:

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-release
```

Visual Studio 2026, if installed:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-release
```

The release package is created at:

```text
build/vs2022/package/SKSE/Plugins/
```

or:

```text
build/vs2026/package/SKSE/Plugins/
```

Expected package contents:

```text
FloatingDamageNumbersNG.dll
FloatingDamageNumbersNG.ini
```

## Building And Running Tests

Core-only tests avoid the SKSE plugin target and are the fastest way to validate the portable logic:

```powershell
cmake --preset tests
cmake --build --preset tests-release
ctest --preset tests-release
```

The tests cover configuration parsing, damage filtering, aggregation, display caps, animation state, formatting, and stress bounds for the core manager.

## Project Layout

```text
assets/                          Repository images and screenshots
include/                         Public project headers
src/                             Plugin and core implementation
src/Skyrim/                      Skyrim/CommonLib runtime integration
tests/                           Core unit tests
package/SKSE/Plugins/            Default runtime INI
CMakeLists.txt                   Main CMake project
CMakePresets.json                Build and test presets
vcpkg.json                       vcpkg manifest
vcpkg-configuration.json         vcpkg registry pins
```

Generated build folders, local output packages, editor state, logs, binaries, and local documentation notes are intentionally ignored.

## Troubleshooting

If the plugin does not load:

- Confirm SKSE matches your exact Skyrim runtime.
- Confirm Address Library or VR Address Library matches your runtime.
- Confirm `FloatingDamageNumbersNG.dll` and `FloatingDamageNumbersNG.ini` are in `Data/SKSE/Plugins/`.
- Check `FloatingDamageNumbersNG.log` in the SKSE log directory.

If numbers do not appear:

- Confirm `bEnabled = true`.
- Confirm the relevant source and target filters are enabled.
- Temporarily set `bDebugLogging = true`.
- Check whether the HUD is hidden or a menu is open.
- Try disabling HUD replacers to test whether dynamic HUD text creation is available.

## Compatibility Notes

Because the plugin does not replace HUD SWFs and does not require SkyUI or MCM, it is intended to coexist with HUD/UI mods such as TrueHUD, SkyHUD, Nordic UI, Untarnished UI, and Dear Diary. If a customized HUD prevents dynamic text creation, the renderer disables itself and logs the failure instead of crashing the game.

## License

This repository is public source-available software, not open source software.

You may view the code, fork it on GitHub, and build it for personal evaluation or personal gameplay. You may not copy, redistribute, publish, sell, sublicense, repackage, or reuse the source code, binaries, configuration files, or other project files outside the permissions written in `LICENSE` without explicit written permission from the copyright holder.

Third-party dependencies remain under their own licenses.
