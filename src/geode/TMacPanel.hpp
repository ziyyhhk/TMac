#pragma once

// Called by the TAB keybind listener. `repeat` is true for key-repeat events.
void tmacOnToggleKey(bool down, bool repeat);

// Closes the window if it is open. Called when the level is closed.
void tmacCloseOpenPanel();
