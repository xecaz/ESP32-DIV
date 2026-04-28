#pragma once

#include "Screen.h"

namespace ui {

// Start the UI task. After this, the UI consumes input::queue(), drives the
// TFT, and manages a small screen stack. Safe to call once after
// input::start().
void start();

// Push a new screen on top of the stack. Takes ownership (delete on pop).
// Thread-safe; can be called from any task.
void push(Screen* screen);

// Pop the top screen. If the stack is empty, noop.
void pop();

// Replace the entire stack with one screen.
void replace(Screen* screen);

// Force the top screen to re-run its onEnter (clears the screen and
// redraws from scratch). Called after a theme change so the new palette
// takes effect immediately instead of waiting for a navigation.
void repaintTop();

} // namespace ui
