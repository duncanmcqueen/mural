# Mural v1.1-plus

This is a fork of [nikivanov/mural](https://github.com/nikivanov/mural) with firmware and web UI improvements for **v1.1 hardware** (bare ESP32, two TMC2209 drivers, MG90s servo). It's based on upstream's `v1.1` tag and won't work on v2 (FluidNC) boards.

## Improvements over v1.1

### Setup
- **Setup steps in any order.** The fixed wizard is replaced by an overview of the four steps (hanger distance, belt position, pen, drawing). Each one shows whether it's done and can be revisited at any time.
- **Settings survive a reboot.** The hanger distance, pen angles and the last uploaded drawing (with its preview) are kept.
- **Resume without homing.** The belt position is saved during setup, so after a power cycle you can carry on without extending to home again. A **Test move to home** checks that the position is still correct.
- Nothing is written to flash while drawing.

### Drawing
- **The web UI stays up while drawing**, showing progress, elapsed time and an estimate of the time left. You can turn this off on the Diagnostics page.
- **Pause, resume and stop.** Pausing lifts the pen after the current short move, and resume puts it back down and carries on from the same spot.
- **Adjust pen pressure while drawing.** Lighter and Firmer buttons move the pen-down angle 1° at a time, for example when the pen starts losing contact near the edges. The new angle is saved when the drawing ends.

### Pen servo
- **Smooth, ramped moves** at a configurable speed, so the pen no longer slams up and down.
- **Soft limits**: a minimum and maximum angle that nothing can command the servo past, so it can't stall against the end of its slot.
- **The pen-up angle can be calibrated** (upstream fixes it at 90°).
- **Gentler start-up.** At boot the servo starts from its last known angle, after a 2 s delay, instead of jumping.
- An optional setting stops driving the servo while the pen is up, to save current.

### Troubleshooting
- **Diagnostics page** (link at the bottom of the main page). It shows reset history with brownout detection, plus servo, motor and storage state. It also has servo test controls and settings, and a log you can view in the browser.
- [HARDWARE_IMPROVEMENTS.md](HARDWARE_IMPROVEMENTS.md) suggests fixes for servo brownouts, servo stalls and hot stepper motors.

### Other
- Serial speed is 115200 baud (upstream uses 9600, which slowed the main loop down).
- The OLED code is skipped, since v1.1 has no OLED. To use one, build with `-DMURAL_HAS_OLED=1`.
- Builds on Windows. `build.py` no longer uses Unix shell commands. If `npm install` fails in `tsc/`, run `npm install --ignore-scripts` there instead.

## Flashing

This fork builds with PlatformIO, like upstream. The firmware and the web UI live in separate flash partitions:

- **Upload** flashes the firmware (`src/`).
- **Upload Filesystem Image** flashes the web UI (`data/`). Do this only when files in `data/` have changed.

Uploading the filesystem image erases the drawing stored on the device, so you'll need to choose the drawing again afterwards. Settings and calibration aren't affected.

# Upstream documentation

Please find the main documentation on https://getmural.me. Note that it now documents v2 hardware.

# Additional Information

## Positioning of the Drawing on the Wall

Here's how the image is prepared and drawn:

- The user defines the pin distance as part of the setup in the UI. For example 1 meter (or 1000mm). (This is d_pins in the image below.)
- The top margin is 20% of that distance, so the top of the image will be 200mm below the line between the two pins.
- Each side also has a 20% margin, so you'll get total of 60% of the horizontal distance, or 600mm.
- Now that we have the max width (600mm). The SVG is resized so its width is 600 and the height gets resized proportionally.
- Then a processing step is performed on the SVG to figure out what to actually draw, with each SVG unit being treated as millimeter.
- Finally it's converted into a simple format for Mural to draw, containing mostly its coordinate movement commands and pen up/down. This file is then uploaded to the microcontroller and executed line by line.

![image_positioning](/images/doc/muralbot_image_positioning.svg)

## Mural's Kinematic Model

Please find the kinematic model [here](KinematicModel.md).
