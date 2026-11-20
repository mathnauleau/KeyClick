# clickkey.ahk: backup if the .exe is blocked

`clickkey.ahk` does the same core job as `keyclick2.exe`: the key left of `1` (`§` or `` ` ``) becomes the left mouse button. It is a small AutoHotkey script, so there is no unsigned `.exe` for Windows to block.

> **Status:** not yet tested on a real Windows machine. Please report anything odd.

## Use it when

- Windows SmartScreen or Smart App Control blocks `keyclick2.exe`, and you would rather not turn protection off
- You prefer a plain text script you can read and edit

## What you get (and don't)

| | `keyclick2.exe` | `clickkey.ahk` |
| --- | --- | --- |
| Key to click | Any key you choose, in a window | The key left of `1` (edit the script to change it) |
| Click type | Left, right or middle | Left click |
| Hold to drag | Setting | Always on |
| Pause shortcut | Ctrl + Alt + K (changeable) | Ctrl + Alt + K |
| Settings window, tray menu, cat | Yes | No (AutoHotkey's own tray icon) |

## Install

1. Install **AutoHotkey v2**. Either run `winget install AutoHotkey.AutoHotkey` in a terminal, or download it from [autohotkey.com](https://www.autohotkey.com). Version 2 is required.
2. Save `clickkey.ahk` in a permanent folder, for example `C:\Users\<you>\Apps\KeyClick\`.
3. Double-click `clickkey.ahk`. A green **H** icon appears in the tray: the script is running.

## Use

- **Click:** press the key left of `1`. Hold it to drag.
- **Pause / resume:** press **Ctrl + Alt + K**. While paused, the key types normally.
- **Quit:** right-click the green **H** tray icon, then **Exit**.

To move the pointer from the keyboard, turn on Windows Mouse Keys (**Left Alt + Left Shift + Num Lock**) and use the numpad arrows.

## Start at login

1. Press **Win + R**, type `shell:startup` and press Enter.
2. Right-click `clickkey.ahk`, choose **Create shortcut**, and move the shortcut into the folder that opened.

## Change the key or the click type

Open `clickkey.ahk` in Notepad. The last line is the mapping:

```autohotkey
SC029::LButton
```

- Left of `::` is the key. `SC029` is the physical key left of `1` on every layout. You can also use a key name, for example `F2::LButton` or `CapsLock::LButton`.
- Right of `::` is the click: `LButton` (left), `RButton` (right) or `MButton` (middle).
- To find the code of another key, right-click the green **H** icon, choose **Open**, then **View → Key history** and press the key.
- You can add several lines, for example one key for left click and another for right click.

Save the file, then right-click the green **H** icon and choose **Reload Script**.

## Uninstall

1. Right-click the green **H** tray icon and choose **Exit**.
2. Delete the startup shortcut (`shell:startup`) and `clickkey.ahk`.
3. Optional: uninstall AutoHotkey from Windows Settings → Apps.

## Limitations

- While the script runs, the mapped key no longer types its character (pause with Ctrl + Alt + K).
- It does not work inside windows running as administrator, unless the script is also run as administrator.
- Don't convert the script to an `.exe` with AutoHotkey's compiler: that file would be unsigned and blocked the same way.
- On a work PC where IT blocks AutoHotkey too, ask IT to allow it.
