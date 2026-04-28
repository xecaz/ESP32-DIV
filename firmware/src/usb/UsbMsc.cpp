// USB Mass Storage stub.
//
// Deferred to M10 polish. Making MSC work cleanly on Arduino-ESP32 needs
// raw SD-sector access, which Arduino's SD library doesn't expose (it
// keeps sdmmc_card_t private in sd_diskio.cpp). Two working paths forward,
// to pick up later:
//   1. Replace SD.begin() with a direct esp-idf sdspi_host_init + our own
//      sdmmc_card_t, then use sdmmc_read_sectors()/sdmmc_write_sectors().
//      Requires re-implementing the FATFS mount too.
//   2. Fork Arduino-ESP32's SD library into our source tree and expose
//      the static sdmmc_card_t pointer.
// For now, mscStart()/mscStop() are compiled no-ops so the rest of the
// USB composite work (CDC, HID, eventually HCI) can land independently.
#include "UsbMsc.h"

#include <Arduino.h>

#include "../hw/Board.h"

namespace usb {

bool mscStart()   { return false; }  // deferred
void mscStop()    {}
bool mscRunning() { return false; }

} // namespace usb
