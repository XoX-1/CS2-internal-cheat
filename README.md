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

## Build

### Prerequisites
- **Windows 10/11**
- **Visual Studio 2022** (with C++ Desktop Development workload)
- **CMake 3.16+**

### Steps

```cmd
git clone https://github.com/XoX-1/CS2-internal-cheat.git
cd CS2-internal-cheat
cmake -B build
cmake --build build --config Release
```

The output DLL will be at `build/Release/mindcheat.dll`.

### Injection
Use any DLL injector to load `mindcheat.dll` into `cs2.exe`. Press **INSERT** to toggle the menu.

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

---

## Disclaimer

This project is for **educational purposes only**. Using cheats in online games violates the game's terms of service and may result in a ban. Use at your own risk.
