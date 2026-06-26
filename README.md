● CTRL//VOID vs cifertech ESP32-DIV — Feature & Architecture Diff                                                                                                       
                                                                                                                                                                        
  1. Architecture                                                               
                                                                                                                                                                        
  ┌───────────────┬────────────────────────────────────────────────────────────────┬────────────────────────────────────────────────────────────────────────────────┐
  │               │                       cifertech (stock)                        │                                   CTRL//VOID                                   │   
  ├───────────────┼────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤   
  │ Build system  │ Arduino IDE, single .ino sketch                                │ PlatformIO + ESP-IDF (Arduino-as-component)                                    │
  ├───────────────┼────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤   
  │ Code          │ Monolithic, ~20 kloc across 10 files                           │ Modular (hw/, input/, radio/, ui/, usb/, storage/)                             │ 
  │ structure     │                                                                │                                                                                │   
  ├───────────────┼────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤
  │ Concurrency   │ Single-threaded; every feature is a                            │ FreeRTOS: dedicated tasks for Input (core 1), UI (core 0), Radio workers (core │   
  │               │ while(!feature_exit_requested) loop hijacking loop()           │  1), Storage (core 1), PCF I²C poller (core 0)                                 │
  ├───────────────┼────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤   
  │ Partition     │ Default                                                        │ Custom 16 MB: NVS / OTA / 4 MB app A / 4 MB app B / 7 MB LittleFS / coredump   │
  │ table         │                                                                │                                                                                │
  └───────────────┴────────────────────────────────────────────────────────────────┴────────────────────────────────────────────────────────────────────────────────┘   
                                                                                                                                                                        
  2. Input pipeline                                                                                                                                                     
                                                                                                                                                                        
  - Stock: bare PCF8574 read in isButtonPressed(), no debounce, hard delay(200) after every handled key. Buttons miss / double-fire / lag while a feature loop is
  running.                                                                                                                                                              
  - Ours: InputTask on core 1 at prio 3 polling every 2 ms; streak-based debounce per key; KeyDown/KeyUp/KeyRepeat/Touch event types. I²C poller decoupled to core 0
  with active bus recovery (periph_module_reset + 9 SCL pulses + STOP), bus-busy register pre-check, freshness gate, popcount + unused-bit invariant filtering. Touch   
  gets 5-point calibration with axis-swap and per-axis invert detection.
                                                                                                                                                                        
  3. UI / navigation                                                                                                                                                    
                                    
  - Stock: each feature paints its own screen, hardcoded colors, no consistent back-navigation.                                                                         
  - Ours: Screen stack (Push/Pop/Replace) with onEnter / onEvent / onTick / onRender / onExit. Hierarchy: MainMenu → SubMenu → Feature → Keyboard (modal). 4 themes
  (Dark / Light / Hacker / Amber) with live-switching propagated to every screen. Common chrome helpers (drawHeader, drawFooter, drawSdWarningIfMissing).               
                                                                                                                                                                        
  4. Keyboard                       
                                                                                                                                                                        
  - Stock: uppercase only, two duplicate keyboardLayout[] decls, space bar silently dropped. WiFi pwd entry effectively unusable.
  - Ours: 4 layouts (lower / upper / numeric / symbols), shift, layer toggle, working space + backspace + enter, arrow-key nav alongside touch, password-mask toggle
  (eye icon), per-screen prompt + start-layer hint.                                                                                                                     
                             
  5. Radio features (parity + extensions)                                                                                                                               
                                                                                
  ┌──────────────────────────────────────────────┬─────────────────────────────────────────────┬───────────────────────────────────────────────────────────────────┐    
  │                  Stock has                   │                   We have                   │                               Notes                               │
  ├──────────────────────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────┤    
  │ Wi-Fi scan, packet mon, deauth, beacon spam, │ ✓ all                                       │ + timestamped PCAP filenames, live pps graph, async connect, AP   │
  │  captive portal                              │                                             │ picker in Wi-Fi Setup                                             │    
  ├──────────────────────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────┤
  │ BLE scan, sniff, jam (NRF24), Sour Apple     │ ✓ all                                       │ + 17-preset BLE Spoofer, BLE-band LEDs                            │    
  ├──────────────────────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────┤
  │ NRF24 spectrum                               │ ✓                                           │ Multi-module block-split = ~3× faster sweep, ISM-only range (84   │    
  │                                              │                                             │ ch fills full screen)                                             │
  ├──────────────────────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────┤    
  │ Protokill                                    │ ✓                                           │ Now lights Wi-Fi + BLE + NRF24 LEDs (multi-band visualisation)    │
  ├──────────────────────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────┤
  │ CC1101 replay / jam                          │ ✓ + saved profiles                          │ OOK detection fixed (was running 2-FSK), tap-to-edit freq         │    
  ├──────────────────────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────┤    
  │ IR record / replay                           │ ✓ + saved profiles                          │ Same approach as CC1101                                           │    
  ├──────────────────────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────┤    
  │ Ducky (BLE)                                  │ partial — Ducky-over-USB-HID landed; BLE    │                                                                   │
  │                                              │ port deferred                               │                                                                   │    
  └──────────────────────────────────────────────┴─────────────────────────────────────────────┴───────────────────────────────────────────────────────────────────┘
                                                                                                                                                                        
  New radio features that didn't exist in stock:                                
  - RadioManager — owner mutex per radio + pin-conflict arbitration (NRF24 #3 vs IR on GPIO 14/21), automatic SD unmount when CC1101/NRF24 claims FSPI.                 
  - leds::signal() — per-event LED signalling on RX/TX with configurable per-channel colors. Stock had 4× WS2812 on board but never drove them.        
                                    
  6. USB stack (entirely new)                                                                                                                                           
                             
  - Stock: only CP2102 UART for flashing/console. No native USB usage.                                                                                                  
  - Ours: TinyUSB composite — MSC (SD) + CDC console + HID keyboard. Ducky-over-USB with arm-and-fire-on-host-connect, scripts on SD /rubberducky/ (any extension, not  
  just .txt). USB host-watch event handler.
                                                                                                                                                                        
  7. USB modes (entirely new — see plan §M8)                                                                                                                            
                                               
  - Standalone — local UI owns the device.                                                                                                                              
  - Bridge — UI runs plus CDC radio-protocol channel (M9, scaffolding only).    
  - Appliance — HackRF-style boot path: no menu, static status card, only SELECT-held-3 s reboots back to Standalone. Confirmation gate before commit.                  
                                                                                
  8. Storage & SD                                                                                                                                                       
                                                                                
  - Stock: SD mounted at boot, no hot-plug, no failure path.                                                                                                            
  - Ours: deferred non-blocking mount (300 ms post-splash); exponential backoff (2→30 s); SD_CD edge detection for hot-plug insertion/removal; async unmount on FSPI
  claim by a radio; "no SD card" red strip overlay on every SD-dependent screen; File Browser tool with two-press delete confirmation.                                  
                                                                                
  9. Settings persistence                                                                                                                                               
                                                                                                                                                                        
  - Stock: JSON file on SD.                                                                                                                                             
  - Ours: NVS Preferences with schema versioning + migration (new defaults retroactively patched on existing devices); per-setting keys for granular updates; sanity    
  clamps for corruption recovery (e.g. brightness < 20 → 200, touch range < 500 → defaults).                                                                            
               
  10. Captive Portal                                                                                                                                                    
                                                                                
  - Stock: HTML hardcoded in C++.                                                                                                                                       
  - Ours: portal serves /portal/* from SD (HTML/CSS/SVG); configurable AP name (Settings); scrollable list of captured submissions with relative timestamps + clear-text
   user/pass.                              
                             
  11. New utility screens (not in stock)                                                                                                                                
                                               
  - Tools → I²C Health — live OK/s, FAIL/s, recovery counter, last-OK age, hit-ratio bar. For empirical bus-mod A/B testing.                                            
  - Tools → USB HID Test — sanity check for HID enumeration.                    
  - Tools → Storage — file browser with delete.                                                                                                                         
  - Settings → Theme — radio-knob picker with swatch previews.
  - Settings → USB Mode — three-mode selector.                                                                                                                          
  - Settings → LEDs — per-channel color editor with hex + swatch.               
  - Settings → Brightness — backlight slider with live preview.                                                                                                         
  - Settings → Wi-Fi Network — SSID/password setup with AP picker, password-visibility toggle, async connect, NTP sync.
  - About → Version / MAC / About / License (4 sub-pages, properly formatted).                                                                                          
                                                                                
  12. Diagnostics & robustness fixes vs stock bugs                                                                                                                      
                                                                                
  ┌─────────────────────────────────────────────────────┬───────────────────────────────────────────────────────────────────────────────┐                               
  │                      Stock bug                      │                                      Fix                                      │
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤                               
  │ Buttons laggy + miss/double-fire                    │ Streak debounce + dedicated input task on core 1                              │                               
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤                               
  │ Keyboard uppercase-only, broken space               │ Multi-layer keyboard with shift                                               │                               
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤                               
  │ Wi-Fi password entry effectively impossible         │ Above + paste-from-AP-pick + show-password                                    │                               
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤                               
  │ LEDs stuck on previous-firmware colors after flash  │ Single-pin blank at boot                                                      │
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤
  │ Touch axis misaligned / mirrored                    │ 5-point calibration with axis-swap + invert detection, persisted in NVS       │
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤
  │ Bad Wi-Fi creds → boot hangs at logo                │ Async connect task + 3-strike skip + storage of fail-count                    │                               
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤                               
  │ PCF I²C glitch reads as "all 5 keys pressed"        │ Popcount + unused-bit invariant filter                                        │                               
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤                               
  │ Deauth esp_wifi_set_mac returns 0x102               │ MAC impersonation before AP start, en_sys_seq=true                            │
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤                               
  │ Packet monitor writes only PCAP 24-byte header      │ Replace esp_wifi_init(nullptr) with proper init + explicit promiscuous filter │
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤                               
  │ Multiple features try to use SPI bus simultaneously │ RadioManager exclusive ownership + SD-unmount-on-release                      │
  ├─────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────────────────────────┤                               
  │ 1-second I²C stalls (this board)                    │ Bus-busy register pre-check + manual SCL recovery + 50 kHz clock              │
  └─────────────────────────────────────────────────────┴───────────────────────────────────────────────────────────────────────────────┘                               
                                                                                
  13. Boot / power                                                                                                                                                      
                                                                                
  - Stock: doesn't survive USB unplug because some features depend on USB CDC pulldowns / bootloader pinning.                                                           
  - Ours: boots and runs on battery alone (IP5306 power management); seeds clock from build timestamp so PCAP filenames are sensible without an RTC; reset-reason logged
   to UART0; CS-parking after the LED blanker so NRF24 #2 can't squat the FSPI bus.                                                                                     
                                                                                
  14. Hardware calibration / mod awareness                                                                                                                              
                                                                                
  - Bodge-wire I²C reroute to GPIO 41/42 documented in Pins.h.                                                                                                          
  - Pins.h is single source of truth for all GPIOs.                             
  - ShieldProfile setting in NVS (Nrf24Triple vs Nrf24DualPlusIR) — RadioManager::acquire() enforces it for the IR/NRF24 #3 pin conflict.  
