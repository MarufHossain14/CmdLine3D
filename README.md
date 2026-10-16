# CmdLine3D

A small ASCII raycast shooter for the terminal. It is intentionally kept in one
C++ file so the rendering, movement, collision, and shooting code are easy to
read and experiment with.

## What is in the game?

- A shaded first-person raycast view
- A dedicated navigation panel with a shortest path around walls
- Color-coded walls, targets, status messages, and map markers
- Responsive movement, precise turning, strafing, and collision detection
- Automatic fine-aim slowdown when a visible target nears the crosshair
- An animated weapon with walking motion, recoil, and muzzle flash
- Five targets that can only be hit when a wall is not in the way
- A crosshair, simple weapon, shot counter, and win message

## Build and run

You need CMake 3.16 or newer and a C++17 compiler.

### Linux or macOS

```sh
cmake -S . -B build
cmake --build build
./build/cmdline3d
```

### Windows (PowerShell)

Run these commands from a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\cmdline3d.exe
```

The game needs an interactive terminal at least 100 columns wide and 32 rows
tall. Windows Terminal, PowerShell, and common Linux/macOS terminals work. On
compatible terminals, the game uses press and release events for especially
smooth held-key movement; older terminals receive an automatic eased fallback.

## Controls

| Key | Action |
| --- | --- |
| `W` / `S` or `Up` / `Down` | Move forward / backward |
| `A` / `D` | Strafe left / right |
| `Q` / `E` or `Left` / `Right` | Turn left / right |
| `Space` | Shoot |
| `X` or `Esc` | Quit |

Aim the `+` crosshair at each red `O`/`H` target and press Space. The panel on
the right shows the full map: `T` is a live target, `x` is a cleared target, the
arrow is you, and `*` marks a safe route around the walls. Follow the instruction
below the map one step at a time. A warning appears if you walk directly into a
wall.
