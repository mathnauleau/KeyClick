#Requires AutoHotkey v2.0
#SingleInstance Force
; clickkey.ahk - no-install alternative to keyclick2.exe
; The key left of "1" (§ or `) becomes the left mouse button. Hold it to drag.
; Ctrl+Alt+K pauses/resumes the remap; while paused the key types normally.

#SuspendExempt
^!k::Suspend
#SuspendExempt False

SC029::LButton
