# DO NOT BUY THIS CHEAT FROM OTHERS. THIS IS THE REAL ONE THAT CREATED BY (MIND) AND FULLY OPEN SOURCE DO NOT FALL FOR SCAMS.

# MindCheat — CS2 Internal Cheat

A feature-rich internal cheat for Counter-Strike 2, built as a DLL that is injected into the game process. Uses ImGui with DirectX 11 for the overlay UI.

**Toggle Menu:** `INSERT`

---

## Features

### Legit
- **Aimbot** — Smooth aim assist with configurable FOV, smoothing, and target bone (head, neck, chest, pelvis, stomach, shoulders, hips). Supports deathmatch (FFA) mode and custom keybind.
- **Triggerbot** — Automatic fire when crosshair is on an enemy. Two fire modes: Tapping and Lazer. Team check option and custom keybind. Keybind conflict detection with aimbot.

### ESP
- **Bounding Box** — Full, Corner, or Rounded box styles
- **Skeleton** — Bone-based skeleton rendering
- **Snaplines** — Lines from screen bottom to player
- **Head Dot** — Dot on enemy heads
- **Health Bar** — Player health indicator
- **Player Names** — Overhead name display
- **Weapon Name** — Currently held weapon
- **Distance** — Distance to player in units
- **Glow** — Enemy and team glow with customizable colors (RGBA)
- **Custom Colors** — Separate enemy/team color pickers

### Visuals
- **No Flash** — Remove flashbang effect
- **No Smoke** — Remove smoke grenade effect
- **FOV Changer** — Override camera FOV (60–150)
- **Radar** — On-screen radar with circular/square styles, configurable size, range, zoom, opacity, enemy/team colors, and optional name/health display

### Misc
- **Bunnyhop** — Auto-hop while holding SPACE
- **Spectator List** — See who is spectating you

### Kill Sound
- **Kill Sound Changer** — Play a custom `.mp3` or `.wav` sound on kills. Browse and select from a file list, with apply/remove controls.

---

## Build Instructions

### Prerequisites
Before you start, make sure you have:
1. **Windows 10/11**
2. **Visual Studio 2022** installed (ensure you have the "Desktop development with C++" workload checked).
3. **CMake** installed (version 3.16 or newer).

### Step-by-Step Guide
1. **Download the Source Code**:
   Open a terminal (like Command Prompt or PowerShell) and run:
   ```cmd
   git clone https://github.com/XoX-1/CS2-internal-cheat.git
   cd CS2-internal-cheat
   ```

2. **Generate Build Files**:
   Tell CMake to prepare the project in a new `build` folder:
   ```cmd
   cmake -B build
   ```

3. **Compile the Cheat**:
   Build the final Release version of the cheat:
   ```cmd
   cmake --build build --config Release
   ```

   Once finished, you will find both the **DLL** (`mindcheat.dll`) and the **Injector** (`mindcheat_injector.exe`) inside the `build/Release/` folder!

### How to Inject (Play)
1. Launch **Counter-Strike 2**.
2. Run the provided `mindcheat_injector.exe` as Administrator (this will automatically inject the DLL into the game).
   *(Alternatively, use your own preferred DLL injector to load `mindcheat.dll` into `cs2.exe`)*
3. Once injected, press **F1** in the lobby, then press the **INSERT** key on your keyboard to open or close the cheat menu!

---

## Project Structure

```
src/
  hooks/         — DX11 hook, main cheat loop, ImGui rendering
  features/      — Aimbot, ESP, Triggerbot, Kill Sound, etc.
  fonts/         — Font Awesome icons for UI
sdk/             — Game offsets, memory utilities, schema system
vendor/          — Third-party libraries (ImGui, MinHook, stb_image)
output/          — Auto-generated offset dumps
```

## Updating Offsets Quickly

If your new dumps are written directly into this project's `output/` folder, just run:

```cmd
update_offsets.exe
```


This script will:
- Replace project `output/` with the new dump
- Refresh `sdk/offsets.hpp` from `output/offsets.hpp`
- Refresh `sdk/client_dll.hpp` from `output/client_dll.hpp`

If your path is a parent folder that contains `output/`, the script detects it automatically.


## Disclaimer

This project is for **educational purposes only**. Using cheats in online games violates the game's terms of service and may result in a ban. Use at your own risk.
