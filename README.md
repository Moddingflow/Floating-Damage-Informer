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

Version 0.2.0 is built with CommonLibSSE-NG 7.2.0. It supports the new
Address Library format 5 and the Skyrim 1.7.x runtime layouts. The DLL declares
an explicit compatibility allowlist so an unknown future Skyrim executable is
rejected until its layouts have been reviewed.

Supported runtimes:

- Skyrim VR 1.4.15.0
- Skyrim SE 1.5.97.0
- Skyrim AE 1.6.317.0, 1.6.318.0, 1.6.323.0, 1.6.342.0, and 1.6.353.0
- Skyrim AE 1.6.629.0 and 1.6.640.0
- Skyrim GOG 1.6.659.0
- Skyrim AE 1.6.1130.0 and 1.6.1170.0
- Skyrim GOG 1.6.1179.0
- Skyrim AE 1.7.99.0
- Skyrim AE 1.7.104.0

The two August 2026 Steam updates require matching runtime components:

| Skyrim runtime | SKSE64 | Address Library |
| --- | --- | --- |
| 1.7.99.0 | 2.3.0 | v12 or the newer all-in-one package |
| 1.7.104.0 | 2.3.1 | v13 |

Skyrim 1.7.99.0 was quickly replaced by 1.7.104.0, but both remain explicitly
supported. Actual loading still requires the SKSE build matching the exact game
executable and the corresponding Address Library database.

### Nexus Compatibility Selection

For a Nexus upload, mark these game versions as compatible:

```text
1.4.15.0 (VR)
1.5.97.0
1.6.317.0
1.6.318.0
1.6.323.0
1.6.342.0
1.6.353.0
1.6.629.0
1.6.640.0
1.6.659.0 (GOG)
1.6.1130.0
1.6.1170.0
1.6.1179.0 (GOG)
1.7.99.0
1.7.104.0
```

If Nexus presents VR compatibility separately from the Skyrim Special Edition
file metadata, select the 14 SE/AE/GOG entries on the Skyrim SE page and mention
VR 1.4.15.0 in the description and requirements. Do not select Epic 1.6.678.0
or Windows Store/Game Pass: SKSE does not support those releases.

## Requirements

### For Players

- Skyrim Special Edition, Anniversary Edition, GOG Anniversary Edition, or Skyrim VR
- The matching Skyrim Script Extender build for your runtime
- Address Library for SKSE Plugins v13, or VR Address Library for SKSEVR

### For Building From Source

- Windows
- Git
- CMake 3.28 or newer
- Visual Studio 2022, or Visual Studio 2026 if you have the newer generator installed
- vcpkg with `VCPKG_ROOT` set
- A vcpkg triplet compatible with the project presets, currently `x64-windows-static-md`

The full SKSE plugin build uses vcpkg for supporting libraries and fetches the
pinned CommonLibSSE-NG 7.2.0 source or its verified release bundle during the
first configure. The core unit tests can be built without the SKSE/CommonLib
dependency graph.

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

The plugin files are created at:

```text
build/vs2022/package/SKSE/Plugins/
```

or:

```text
build/vs2026/package/SKSE/Plugins/
```

The complete release package contains:

```text
package/
  CREDITS.md
  LICENSE.txt
  THIRD_PARTY_NOTICES.md
  licenses/
    CommonLibSSE-NG-COPYING.txt
    CommonLibSSE-NG-EXCEPTIONS.md
    OpenVR-LICENSE.txt
    DirectXMath-LICENSE.txt
    DirectXTK-LICENSE.txt
    fmt-LICENSE.txt
    rapidcsv-LICENSE.txt
    spdlog-LICENSE.txt
  SKSE/
    Plugins/
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

A full plugin build also adds `FDNPluginMetadataTests`, which loads the produced
DLL and verifies its SKSE exports, plugin version, and complete 15-runtime
compatibility declaration:

```powershell
ctest --test-dir build/vs2022 -C Release --output-on-failure
```

## Project Layout

```text
assets/                          Repository images and screenshots
include/                         Public project headers
src/                             Plugin and core implementation
src/Skyrim/                      Skyrim/CommonLib runtime integration
tests/                           Core and compiled-plugin metadata tests
package/SKSE/Plugins/            Default runtime INI
CMakeLists.txt                   Main CMake project
CMakePresets.json                Build and test presets
vcpkg.json                       vcpkg manifest
vcpkg-configuration.json         vcpkg baseline pin
CREDITS.md                       Project and dependency credits
THIRD_PARTY_NOTICES.md           Binary dependency and source notice
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

## Credits

- TheWhistle — Floating Damage Informer design, implementation, and maintenance.
- The SKSE Team — Skyrim Script Extender.
- meh321 — Address Library for SKSE Plugins.
- Alan Tse (alandtse) and the CommonLibSSE-NG contributors — CommonLibSSE-NG
  and multi-runtime/VR support.
- Alan Tse (alandtse) and contributors — VR Address Library for SKSEVR.
- Valve Corporation — OpenVR.
- Microsoft — DirectXMath and DirectX Tool Kit.
- Victor Zverovich and contributors — {fmt}.
- Gabi Melman and contributors — spdlog.
- Kristofer Berggren — rapidcsv.
- Bethesda Game Studios — The Elder Scrolls V: Skyrim Special Edition and
  Skyrim VR. No Bethesda game assets are distributed with this mod.

Full links and attribution details are available in `CREDITS.md`. License texts
and source notices are listed separately in `THIRD_PARTY_NOTICES.md`.

## License

Floating Damage Informer source code is licensed under the MIT License. See
`LICENSE`.

The compiled DLL statically links CommonLibSSE-NG, which is distributed under
GPL-3.0-or-later with its stated exceptions. Release packages include the
applicable license texts and `THIRD_PARTY_NOTICES.md`; distributors must also
make the corresponding source available. Other third-party dependencies remain
under their own licenses.
