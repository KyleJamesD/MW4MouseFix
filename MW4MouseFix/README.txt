```
MW4 MOUSE FIX - README
======================

What This Is:
  A DirectInput8 wrapper DLL that removes mouse acceleration
  and smoothing from MechWarrior 4 Vengeance and Black Knight.

How To Build:
  1. Open a command prompt
  2. cd C:\Projects\MW4MouseFix (or whatever directory the MW4MouseFix folder is)
  3. Run build.bat with .\build.bat
  4. Output goes to the build folder

How To Install:
  Copy these two files next to the game's .exe:
    build\dinput8.dll
    mousefix.ini

  For Vengeance:  put files in the root MechWarrior 4 folder where the .exe is
  For Black Knight: put files in the MW4X subfolder where the .exe is

How To Uninstall:
  Delete these files from each game folder:
    dinput8.dll
    mousefix.ini
    mousefix.log  (created when game runs)

Settings (mousefix.ini):
  SensitivityY10=30  (30 means 3.0x, change to taste)(can change deadzone of mouse as well as X axis input) 
  DebugLog=1         (set to 0 to disable logging)

Built With:
  Visual Studio 2022 (32-bit x86 compiler)
  Windows 10/11
```