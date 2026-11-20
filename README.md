# KeyClick

Turn any key on your keyboard into a mouse button. By default it is the key left of `1` (`§` or `` ` ``, depending on your layout), acting as a left click.

Useful as an accessibility aid: pair it with Windows **Mouse Keys** (move the pointer with the numpad, click with your chosen key) or with any other pointing device that moves but can't click comfortably.

> **Status:** built and compiled on Linux with Zig and checked for build errors, but not yet tested on a real Windows machine. Please report anything odd.

## Requirements

- Windows 10 or 11, 64-bit
- Nothing to install. It is one portable `.exe`.

## Install

1. Put `keyclick2.exe` in a permanent folder, for example `C:\Users\<you>\Apps\KeyClick\`.
2. If you used the earlier `keyclick.exe`, quit it first (right-click its tray icon, **Quit**).
3. Double-click `keyclick2.exe`.
4. If Windows SmartScreen says "Windows protected your PC", click **More info**, then **Run anyway**. The file is unsigned, and antivirus tools may also flag it because it uses a keyboard hook.
5. Optional: switch on **Start with Windows** in the window. If you move the file later, switch it off and on again.

## Use

The window has these settings. Changes apply right away.

| Setting | What it does |
| --- | --- |
| On/off switch | Turns KeyClick on or off. While off, your key types normally. |
| **Key** → **Change key** | Press **Change key**, then press the key you want. **Esc** cancels. The Windows keys can't be used. |
| **Acts as** | Left click, right click or middle click. |
| **Hold to drag** | On: holding the key holds the button down, so you can drag. Off: each key press is a single click. |
| **Pause shortcut** | A global shortcut (default **Ctrl + Alt + K**) that turns KeyClick on or off from anywhere. It needs at least one modifier key. |
| **Start with Windows** | Starts KeyClick in the tray when you sign in. |

Closing the window hides it to the tray. **Right-click** the tray icon for **Enabled**, **Settings…** and **Quit**. **Left-click** it to open the window.

Keyboard: **Tab** moves between controls, **Space** or **Enter** activates one.

## Moving the pointer

KeyClick only provides the click. To move the pointer from the keyboard, turn on Windows Mouse Keys (**Left Alt + Left Shift + Num Lock**, or Settings → Accessibility → Mouse) and use the numpad arrows.

## Files and registry

- Settings: `%APPDATA%\KeyClick\settings.ini`
- Start with Windows: one value named `KeyClick` under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`

## Uninstall

1. Switch off **Start with Windows**.
2. Right-click the tray icon and choose **Quit**.
3. Delete `keyclick2.exe` and the `%APPDATA%\KeyClick` folder.

## Limitations

- While KeyClick is on, the chosen key no longer types its character.
- It doesn't work inside windows running as administrator (unless KeyClick is also run as administrator), nor on the sign-in screen or UAC prompts.
- Screen readers read each control's name and state (for example "Hold to drag on"), but the controls are custom-drawn rather than standard Windows controls.
- Only one instance runs at a time. Starting it again brings the window forward.

## Build from source

Source: `keyclick2.c` and `cat_frames.h` (the animation frames). Keep them in the same folder. With [Zig](https://ziglang.org) as the compiler:

```
zig cc -target x86_64-windows-gnu -O2 -s -Wl,--subsystem,windows keyclick2.c -o keyclick2.exe -luser32 -lgdi32 -lshell32 -ladvapi32 -lgdiplus
```

## Repository layout

- `keyclick2.exe`: the current build (settings window, tray, animated cat)
- `keyclick2.c`, `cat_frames.h`: its source; the frames were generated from the sprite in `tools/make_cat34.py`
- `clickkey.ahk`: no-install alternative for PCs that block the `.exe` (needs [AutoHotkey v2](https://www.autohotkey.com)); same key-to-click mapping, no window
- `v1/`: the first, tray-only version with a fixed key (`keyclick.c`, `keyclick.exe`)
- `tools/make_cat34.py`: draws the pixel cat and writes the GIF (needs Pillow)
- `art/cat34.gif`: the cat animation (12 frames, transparent)
