# CmdLine3D

A small ASCII raycast shooter for the terminal. It is intentionally kept in one
C++ file so the rendering, movement, collision, and shooting code are easy to
read and experiment with.

## What is in the game?

- A shaded first-person raycast view
- A minimap with walls (`#`), targets (`t`), and the player arrow
- Collision-aware movement
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
tall. Windows Terminal, PowerShell, and common Linux/macOS terminals work.

## Controls

| Key | Action |
| --- | --- |
| `W` / `S` | Move forward / backward |
| `A` / `D` | Turn left / right |
| `Space` | Shoot |
| `X` or `Esc` | Quit |

Aim the `+` crosshair at each `@`/`M` target and press Space. The minimap in the
top-left helps you navigate around walls.
