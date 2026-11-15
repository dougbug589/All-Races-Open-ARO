// input.h - lets the menu take the keyboard and gamepad away from the game while it is open.
#pragma once
#include <windows.h>
#include <xinput.h>

namespace input {

void install();                          // as early as possible (before the game looks up XInput)
void block(bool on);                     // true: the game sees no keys and a centred, idle gamepad
bool padState(int index, XINPUT_STATE* out);   // read a gamepad directly (ignores the block)
bool anyHeld();                          // any gamepad button or keyboard key still held?

}  // namespace input
