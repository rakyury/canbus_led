# Suggested CAN frame format

You can adjust identifiers to match your vehicle. The table below shows a simple option using standard 11-bit frames.

| ID   | Description | Data fields |
| ---: | ----------- | ----------- |
| 0x100 | Throttle pedal | `data[0]` — pressed percent 0–100. Remaining bytes unused. |
| 0x101 | Brakes/handbrake | `data[0]` bit0 — brake pedal, bit1 — handbrake. |
| 0x102 | Engine RPM | `data[0..1]` — RPM (little endian). |
| 0x103 | Coolant temperature | `data[0..1]` — temperature ×10 °C (e.g., 900 = 90.0°C). |
| 0x104 | Rev limiter | `data[0]` ≠ 0 when limiter is active. |
| 0x105 | ALS (anti-lag) | `data[0]` ≠ 0 when ALS is active. |
| 0x106 | Oil pressure | `data[0..1]` — pressure ×10 kPa (≈0.1 bar). |

## Display logic
- **Throttle:** green bar across the strip. Filled length matches pedal percentage.
- **RPM:** blue-to-yellow gradient drawn across the width proportional to RPM. When above `rpmRedline` (default 6500) the strip pulses red.
- **Coolant temperature:** the last LED shows a gradient from blue (60°C) to green (85°C) to red (110°C).
- **Brake:** the entire strip overlays red.
- **Handbrake:** first quarter receives a purple overlay.
- **Rev limiter:** yellow pulsing overlay on the entire strip.
- **ALS:** amber pulse across the strip to indicate anti-lag enabled.
- **Warmup:** when `coolant < 60°C` a soft breathing blue overlay is applied.
- **Oil pressure panic:** if `throttle > 40%` and pressure <2 bar the strip strobes red/white rapidly.

Update the `ID_*` constants and related parameters in `src/main.cpp` as needed.
