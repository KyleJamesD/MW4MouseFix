# MW4 Mouse Fix (Simple Edition)

A lightweight DirectInput8 proxy DLL that scales mouse X/Y deltas by configurable percentages.

## Features
- Works as a drop-in `dinput8.dll` replacement for MechWarrior 4
- Only adjusts sensitivity—no deadzone or nonlinear magic
- Configurable via `mousefix.ini`

## Configuration
Edit `mousefix.ini` alongside the DLL:

```
[MouseFix]
ScalePercentX=125
ScalePercentY=125
DebugLog=0
```

- `ScalePercentX` / `ScalePercentY`: integer percentages. `100` = default sensitivity.
- `DebugLog`: set to `1` to create `mousefix.log` with detailed diagnostics.

## Building
1. Open a **x86 Native Tools** Developer Command Prompt for Visual Studio (2019 or newer).
2. Run:

```bat
cd MW4MouseFix2Simple
build.bat
```

Output DLL is placed in `build\dinput8.dll`.

## Installation
1. Copy `build\dinput8.dll` and `mousefix.ini` into your MechWarrior 4 game directory.
2. (Optional) enable `DebugLog=1` to verify scaling adjustments in `mousefix.log`.

That’s it—tune the percentages to taste and enjoy smoother mouse control.
