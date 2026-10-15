# MapFix: Deadly Premonition Enhanced Map Mod

[![Platform](https://img.shields.io/badge/Platform-Windows%20(x86)-blue.svg)]()
[![Game](https://img.shields.io/badge/Game-Deadly%20Premonition%3A%20The%20Director's%20Cut-crimson.svg)]()
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

An open-source quality-of-life ASI modification for **Deadly Premonition: The Director's Cut (PC)**.

In the vanilla game, the in-game map viewer is severely constrained:
- Zoom-out is artificially clamped to `1.0`, preventing players from seeing the full overworld.
- Map camera bounds are tightly restricted by zoom-dependent edge formulas.
- The game's streaming engine only maintains a $10,000 \times 10,000$ unit ($3 \times 3$ tile) window around the cursor, actively unloading and culling distant tiles as the cursor pans across Greenvale.

**MapFix** completely fixes these limitations by patching the engine's memory in-place, allowing seamless, persistent rendering of all **64 Greenvale overworld map tiles** simultaneously.

---

## Features

- 🗺️ **Full 64-Tile Overworld Rendering**: Replaces the vanilla 9-tile loop restriction with a full 64-tile rendering loop (`0x0065A2ED` & `0x0065A43C`), drawing `SMAP00_00_000.XPC` through `SMAP00_00_063.XPC` across the entire map canvas.
- 🔍 **Unlocked Zoom-Out (`0.20x`)**: Removes the vanilla `1.0` floor clamp (`0x0065919F`), allowing smooth zoom-out down to `0.20x` to view all of Greenvale at a glance.
- 🛡️ **Tile Deletion & Release Immunity**: Hooks `ResourceManager::ReleaseResource` (`0x006B2830`) and `DeleteResource` (`0x006B3AA0`) to protect all 64 overworld tiles (IDs `0x0937..0x0976`) from ever being evicted or marked for unload while panning.
- ⚡ **On-Demand Auto-Load Hook**: Hooks `FindResourceByName` (`0x0065A32A`) in the render loop so that any tile missing from RAM is synchronously loaded on-the-spot without popping or blank gaps.
- 🧭 **Synchronous Preloader**: Loads all 64 tiles synchronously when the map viewer is opened (`0x00661860`), ensuring instantaneous rendering before the first frame.
- 🎮 **Full Panning Freedom**: Neutralizes camera edge clamping (`0x00659534` / `0x00659574`), granting full WASD / analog stick cursor movement across the entire `40000x40000` world space.
- 🧭 **Map Orientation Lock (North-Up / South-Up)**: Overrides the disorienting dynamic map rotation (`0x00659499`..`0x0065D6B3`) so the map stays locked to North-Up (`LockOrientation = 1`) or South-Up (`LockOrientation = 2`), while decoupling York's player arrow (`0x0065C27B`) so it accurately rotates independently on the fixed map canvas.
- ⚙️ **Customizable Configuration**: Configure initial zoom, minimum zoom floor, map orientation lock, and debug logging through `MapFix.ini`.

---

## Installation

1. Ensure you have **Ultimate ASI Loader** installed in your game directory (usually `d3d9.dll` in the same directory as `DP.exe`).
2. Download or build `MapFix.asi` and `MapFix.ini`.
3. Place `MapFix.asi` and `MapFix.ini` into the `scripts/` directory inside your Deadly Premonition root folder:
   ```
   Deadly Premonition The Director's Cut/
   ├── DP.exe
   ├── d3d9.dll             (Ultimate ASI Loader)
   └── scripts/
       ├── MapFix.asi
       └── MapFix.ini
   ```
4. Launch the game and open the map!

---

## Configuration (`MapFix.ini`)

The mod can be configured via `scripts/MapFix.ini`:

```ini
[MapFix]
; 1 = Enabled, 0 = Disabled
Enabled = 1

; Map orientation lock: 0 = Dynamic (Vanilla), 1 = North-Up (0 deg), 2 = South-Up (180 deg)
LockOrientation = 1

; Minimum zoom limit (vanilla is 1.0, 0.20 shows the entire Greenvale map)
MinZoom = 0.20

; Default zoom level when opening the map menu (0.20 opens fully zoomed out)
DefaultZoom = 0.20

; Enable debug logging to scripts\MapFix.log (1 = yes, 0 = no)
EnableLog = 1
```

---

## Technical Architecture & Reverse Engineering

| Target Address | Description | Original Bytes | Patched Action |
|---|---|---|---|
| `0x0065919F` | Zoom-out floor clamp | `75 0D` (`jne`) | `EB 0D` (`jmp`, skips 1.0 floor clamp) |
| `0x006591F0` | Frame zoom clamp call | `E8 2B 74 0A 00` | Hooks `ZoomClampWrapper` (enforces `g_MinZoom` .. `3.5f`) |
| `0x00661860` | Map viewer init hook | `D9 9E E0 8E 00 00` | Hooks `ZoomInitHook` (sets initial zoom & preloads 64 tiles) |
| `0x00659534` | Camera X edge clamp | `D9 44 24 3C` | `D9 EE 90 90` (`fldz`, unclamps X pan boundary) |
| `0x00659574` | Camera Z edge clamp | `D9 44 24 38` | `D9 EE 90 90` (`fldz`, unclamps Z pan boundary) |
| `0x0065A2ED` | Tile index lookup (3x3) | `0F B7 84 54 68 07 00 00 ...` | `89 D3` (`mov ebx, edx` + 18 NOPs, loops 0..63) |
| `0x0065A43C` | Tile loop limit | `83 F8 09` (`cmp eax, 9`) | `83 F8 40` (`cmp eax, 64`) |
| `0x006B3AA0` | `DeleteResource` | `55 8B EC 83 EC 0C` | Hooks `DeleteResourceHook` (bypasses deletion for `0x0937..0x0976`) |
| `0x006B2830` | `ReleaseResource` | `55 8B EC 83 EC 08` | Hooks `ReleaseResourceHook` (bypasses release with `ret 12` for `0x0937..0x0976`) |
| `0x0065A32A` | `FindResourceByName` | `E8 01 89 05 00` | Hooks `FindOrLoadTileResourceHook` (on-demand synchronous fallback) |
| `0x00659499` | Map heading hook (camera/render) | `E8 xx xx xx xx D9 40 7C` | Hooks `MapHeadingHook` (locks orientation: 0=Dynamic, 1=North-Up, 2=South-Up) |
| `0x006594D6` | Map heading hook | `E8 xx xx xx xx D9 40 7C` | Hooks `MapHeadingHook` |
| `0x0065A246` | Map heading hook | `E8 xx xx xx xx D9 40 7C` | Hooks `MapHeadingHook` |
| `0x0065A4E9` | Map heading hook | `E8 xx xx xx xx D9 40 7C` | Hooks `MapHeadingHook` |
| `0x0065BA14` | Map heading hook | `E8 xx xx xx xx D9 40 7C` | Hooks `MapHeadingHook` |
| `0x0065C1EE` | Map heading hook | `E8 xx xx xx xx D9 40 7C` | Hooks `MapHeadingHook` |
| `0x0065D6B3` | Map heading hook | `E8 xx xx xx xx D9 40 7C` | Hooks `MapHeadingHook` |
| `0x0065C27B` | York arrow icon rotation | `D9 EE 8B D8 D9 5B 48` | Hooks `YorkArrowIconHook` (counter-rotates arrow on locked map) |

---

## Building from Source

### Using Zig (Recommended)
You can compile directly using `Zig cc`:
```cmd
zig cc -target x86-windows -O2 -shared src\MapFix.c -o build\MapFix.asi
```
Or run the included `build.bat`:
```cmd
build.bat
```

### Using CMake & MSVC
```cmd
mkdir build
cd build
cmake -A Win32 ..
cmake --build . --config Release
```

---

## License

This project is licensed under the [MIT License](LICENSE).
Deadly Premonition is a trademark of Access Games / Marvelous Inc. / Toybox Inc. This project is unaffiliated with the original developers or publishers.
