#pragma once

#include "../input/InputEvent.h"

class TFT_eSPI;

namespace ui {

// Abstract base for every screen: main menu, feature, keyboard, ...
// Screens are owned by UiTask on a stack. The active screen's onEvent runs
// for every InputEvent; onTick runs every render tick; onRender is called
// when the screen asks for a repaint.
class Screen {
public:
    virtual ~Screen() = default;

    // Called when the screen is pushed (becomes active). Use to allocate UI
    // state and mark dirty.
    virtual void onEnter(TFT_eSPI& tft) = 0;

    // Called when another screen is pushed on top, or this one is popped.
    virtual void onExit(TFT_eSPI& tft) {}

    // Called for every input event while this screen is on top of the stack.
    // Return true to consume the event (UiTask won't do anything else with
    // it), false to let it bubble.
    virtual bool onEvent(const input::Event& e) { (void)e; return false; }

    // Periodic tick (~30 Hz). Use for animations, polling feature state.
    virtual void onTick(uint32_t nowMs) { (void)nowMs; }

    // Called when the screen has marked itself dirty. Draw to `tft`.
    virtual void onRender(TFT_eSPI& tft) = 0;

    // Screens call dirty() to request a repaint on the next tick.
    void dirty()       { dirty_ = true; }
    bool consumeDirty() { bool d = dirty_; dirty_ = false; return d; }

private:
    bool dirty_ = true; // start dirty so onEnter triggers a first paint
};

} // namespace ui
