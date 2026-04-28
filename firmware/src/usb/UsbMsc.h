#pragma once

namespace usb {

// USB Mass Storage: expose the SD card as a removable USB drive. Must be
// called after board::mountSd() has succeeded at least once (the MSC
// descriptors use the card's real sector count/size).
//
// While MSC is active, the SD card is effectively owned by the host —
// firmware-side writes will collide with host reads and corrupt the FAT.
// For now we only start MSC on explicit user request (Settings → USB Mode,
// or the Appliance-mode boot path); scheduled for M10 polish is a "share"
// mode where both sides can read but only one writes at a time.
bool mscStart();
void mscStop();
bool mscRunning();

} // namespace usb
