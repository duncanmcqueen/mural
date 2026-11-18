# Hardware improvements (v1.1)

Suggested hardware changes for the servo stalling / voltage drop / brownout problems, and for better troubleshooting. None of these are required by the firmware. Items that would need firmware support are marked **(firmware)**.

## Power and the servo (likely causes of the brownouts)

At the moment the servo and the ESP32 share the LM2596's 5V output. A stalled MG90s can draw 0.7–1 A, and that drags the shared rail down enough to trip the ESP32's brownout detector.

1. **Add a bulk capacitor at the servo**: 470–1000 µF, 10 V or higher, across the servo's 5V and GND, soldered as close to the servo connector as possible. It absorbs the current spike when the servo starts moving. This is the cheapest fix and the one to try first.
2. **Use thicker wire for the 5V and GND to the servo and ESP32**, rather than Dupont jumpers. Thin jumpers and crimp contacts add resistance that turns current spikes into voltage drops. Also check that every Dupont contact grips firmly.
3. **Check the buck output under load.** Set the LM2596 to 5.0–5.2 V, measured while the servo moves. Don't set it below 5 V: the ESP32's VIN regulator needs some headroom.
4. **Give the servo its own supply (best fix).** Use a second small buck module (or a 5V/3A UBEC) from the 12V rail, just for the servo. Connect its GND to the ESP32's GND, and the signal wire to GPIO 2 as before. Servo current spikes then can't reach the ESP32 at all.
5. **Add a small capacitor at the ESP32**: 100–470 µF across VIN/GND on the ESP32 board as well, if brownouts continue after the above.

## Mechanical (servo stalling or shuddering)

6. **Check the horn and slot alignment.** If the horn is one spline tooth off, the "up" or "down" angle can push into the end of the slot. The servo then stalls and draws its full stall current continuously. Re-seat the horn so that the up and down angles both sit within the free travel.
   - Firmware help: set **soft limits** on the Diagnostics page (min/max angle) just inside the free travel. Nothing can then command the servo past them.
   - Firmware help: **"Release servo when pen is up"** stops the holding current while the pen is up.
7. **Check for binding.** With the servo released (Diagnostics → Release), move the pen carriage by hand. It should slide without catching. Look for elephant's foot or stringing on the printed parts in the slot, and a pen clamp screw that's too tight.

## Measuring (needs new firmware code to read)

8. **Supply voltage sensing (firmware).** A resistor divider into an ADC1 pin. Use only ADC1 pins (GPIO 32–39), because ADC2 doesn't work while WiFi is on. GPIO 34 and 35 are free and input-only.
   - 12V rail: 100 kΩ (top) + 10 kΩ (bottom) to GPIO 34. That gives 1.09 V at 12 V, with headroom up to about 30 V.
   - 5V rail: 10 kΩ + 10 kΩ to GPIO 35. That gives 2.5 V at 5 V.
   - Optionally, add 100 nF from each ADC pin to GND to filter noise.
   - The firmware could then log the minimum voltage during every servo move, so sags show up even when they don't cause a reset.
9. **Servo current sensing (firmware).** An INA219 module (I²C, about $2) in series with the servo's 5V supply. It measures servo current and voltage, which makes stall detection (high current while holding) straightforward. It shares the I²C pins (GPIO 21/22) that the OLED would use.
10. **Real servo position feedback (firmware, optional mod).** Solder a thin wire to the wiper (middle pin) of the servo's internal potentiometer and run it to a spare ADC1 pin (for example GPIO 32 or 33). Firmware could then compare the commanded angle with the actual angle and detect "told 30°, stuck at 42°". Some servos sold as "feedback servos" have this wire already.

## Free GPIO summary (v1.1 wiring)

| In use | Pins |
|---|---|
| Left stepper STEP / DIR / EN | 13 / 12 / 14 |
| Right stepper STEP / DIR / EN | 27 / 26 / 25 |
| Servo | 2 |
| I²C (OLED, if fitted) | 21 (SDA) / 22 (SCL) |

**Free ADC1 pins: 32, 33, 34, 35, 36 (VP), 39 (VN).** 34–39 are input-only, which is fine for sensing.
