#pragma once

#include <stdint.h>

namespace input {

enum class Key : uint8_t {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    Select,
};

enum class EventType : uint8_t {
    KeyDown,     // rising edge (after debounce)
    KeyUp,       // falling edge (after debounce)
    KeyRepeat,   // auto-repeat fire while held
    TouchDown,   // first reading above pressure threshold
    TouchMove,   // subsequent reading while touched
    TouchUp,     // released
};

struct Event {
    EventType type;
    Key       key;   // valid for KeyDown / KeyUp / KeyRepeat
    int16_t   x;     // valid for Touch*
    int16_t   y;     // valid for Touch*
    uint16_t  z;     // touch pressure; valid for Touch*
    uint32_t  tMs;   // millis() at event
};

const char* keyName(Key k);
const char* typeName(EventType t);

} // namespace input
