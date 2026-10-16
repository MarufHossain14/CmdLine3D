# CmdLine3D

A compact SDL3 pixel-art raycast shooter written in C++17. The game renders to
a 320×200 software framebuffer and scales it to a crisp 960×600 window, giving
it a deliberately retro look without terminal rendering or input limitations.

## Features

- Smooth native keyboard and mouse input
- Textured and distance-shaded raycast walls
- Pixel-art targets, weapon animation, recoil, and muzzle flash
- Collision-aware movement and wall-aware shooting
- Minimap with a shortest route to the next target
- Resizable letterboxed window with sharp nearest-neighbor scaling

## Build and run

You need CMake 3.16 or newer, a C++17 compiler, and SDL3.

### Linux

```sh
cmake -S . -B build
cmake --build build
./build/cmdline3d
```

### Windows (Developer PowerShell)

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\cmdline3d.exe
```

CMake locates SDL3 through its installed `SDL3Config.cmake` package.

## Controls

| Input | Action |
| --- | --- |
| `W` / `S` or `Up` / `Down` | Move forward / backward |
| `A` / `D` | Strafe left / right |
| Mouse or `Q` / `E` | Aim left / right |
| Left click or `Space` | Fire |
| `Tab` | Release or recapture the mouse |
| `Esc` | Quit |

The navigation panel marks live targets in red, cleared targets in gray, and a
safe route in yellow. Follow its `NEXT` instruction when a wall blocks the
direct path.
