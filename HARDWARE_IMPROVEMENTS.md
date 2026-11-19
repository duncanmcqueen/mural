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

## Stepper motor heat (motors at 70 °C+, PLA softens around 55–60 °C)

On v1.1 the TMC2209s run standalone. The motor current is set by each driver's Vref potentiometer, not by the firmware, and the drivers apply it continuously, including while holding still. Heat goes up with the square of the current, and Mural's load is light: upstream v2 runs its drivers at 0.707 A RMS.

A. **Lower the driver current (free, biggest effect).** On the BTT TMC2209 (0.11 Ω sense resistors), Vref ≈ 1.41 × I<sub>rms</sub>.

   | Target | Vref | Heat compared with 1.0 A |
   |---|---|---|
   | 1.0 A | 1.41 V | 100% |
   | 0.8 A | 1.13 V | ~64% |
   | 0.7 A | 0.99 V | ~49% |
   | 0.6 A | 0.85 V | ~36% |

   1. With 12 V on and the motors idle, measure between the driver's GND and the potentiometer's centre (or its Vref pad). Note the starting value.
   2. Set both drivers to about 0.85–0.9 V.
   3. Re-home, draw something, then use **Test move to home**. If the pen returns to exactly the same spot, no steps were skipped. Check the top corners in particular, where belt tension is highest.
   4. If steps are skipped, raise Vref by 0.1 V and test again.

B. **Motor carriers in a heat-resistant material.** Reprint `left motor carrier` and `right motor carrier` in PETG (softens at about 80 °C) or ASA/ABS (about 100 °C). They're the parts in contact with the motors.

C. **Heatsinks and airflow.** Stick a small aluminium heatsink (about 20–40 mm) on the back of each motor. Add vent slots if the cover encloses the motors. A small 5 V fan from the buck output is optional.

D. **Driver UART wiring (firmware).** Connect each TMC2209's PDN/UART pin to the ESP32, as v2 does with GPIO 16/17 and addresses set by MS1/MS2. The firmware could then set the running current in software, use a lower holding current at standstill, and enable CoolStep, which reduces the current automatically under light load. This needs driver boards or extension boards that route the PDN/UART pin.

What doesn't help much: slowing down (heat depends on current, not speed) or changing the supply voltage (the driver regulates current regardless). Turning the motors off when idle isn't an option, because they hold Mural on the wall.

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
