#pragma once

#include <Arduino.h>   // String
#include <stdint.h>

namespace radio {
namespace ir {

struct IrCapture {
    uint32_t  decodedValue;  // decoded NEC/RC5/etc value, 0 if unknown
    uint16_t  bits;
    uint8_t   protocol;      // decode_type_t from IRremoteESP8266
    uint16_t  khz;           // carrier frequency, typically 38
    // Raw timings (mark/space alternation) for replaying unknown signals.
    uint16_t  rawLen;
    uint16_t  rawBuf[200];
};

// Start continuous IR receive. Capture is stored in the "latest" slot the
// moment a decoded or raw signal arrives.
bool startRx();
void stopRx();
bool rxRunning();
bool haveCapture();
bool getCapture(IrCapture& out);
void clearCapture();

// Transmit a previously captured IR frame.
bool txCapture(const IrCapture& c);

// ── Saved profiles ─────────────────────────────────────────────────────
// Binary file format at /ir/ir_NNNN.bin. Header + rawBuf; matches the
// schema the stock firmware used so old captures still load.
struct __attribute__((packed)) SavedHeader {
    uint32_t magic;       // 'IRPF'
    uint16_t version;     // 1
    uint16_t khz;         // carrier (usually 38)
    uint16_t rawLen;      // entries in rawBuf
    uint8_t  decodeType;  // decode_type_t
    uint8_t  reserved;
    uint16_t bits;
    uint64_t value;       // decoded value (if known)
    char     name[16];
};

// Save `c` to SD, auto-incremented filename. Returns the filename written
// or empty on failure. `label` is copied into the header's name[] field.
String saveCapture(const IrCapture& c, const char* label = "");

// List all saved captures (just basenames, sorted). Returns count; if `out`
// is non-null, writes up to `max` basenames into it.
int listSaved(String* out, int max);

// Load a saved capture by basename (e.g. "ir_0007.bin"). Returns true on
// success; on failure, `c` is left unchanged.
bool loadCapture(const String& basename, IrCapture& c);

// Delete a saved capture. Returns true if the file was removed.
bool deleteSaved(const String& basename);

} // namespace ir
} // namespace radio
