# Wiring TTGO T-CAN48 to a vehicle and LED strip

## CAN bus
- The built-in TTGO T-CAN48 transceiver connects to the vehicle CANH and CANL through the onboard terminal block.
- Bus speed in firmware is fixed at **1 Mbps**.
- Default ESP32 pins are `GPIO21` (TX) and `GPIO22` (RX). Check the silk screen and adjust in `src/main.cpp` if needed.
- Tie the board ground (GND) to vehicle ground for stable CAN operation.
- If the tap point lacks termination, place a 120 Ω load between CANH and CANL (some T-CAN48 revisions provide a switch).

## Addressable strip (WS2812/Neopixel)
- Feed the strip with regulated 5 V power and share GND with the board.
- Connect the strip data wire (DIN) to `GPIO4` (changeable in `src/main.cpp`).
- For long strips add a 220–470 Ω series resistor on DIN and a 1000 µF capacitor across 5 V and GND at the strip start.

## USB/UART
- Logs stream at **115200 baud** to show CAN status and firmware boot details.
