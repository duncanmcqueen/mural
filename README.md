# Mural v1.1-plus

This is a fork of [nikivanov/mural](https://github.com/nikivanov/mural) that builds directly on the **v1.1-plus** firmware and web UI work by Jonathon T in [zeiphon/mural-1-plus](https://github.com/zeiphon/mural-1-plus). Everything described under "Improvements over v1.1" below is his work, except the items marked *(this fork)*; the full list is under [Credits](#credits). It targets **v1.1 hardware** (bare ESP32, two TMC2209 drivers, MG90s servo), is based on upstream's `v1.1` tag, and won't work on v2 (FluidNC) boards.

## Improvements over v1.1

### Setup
- **Setup steps in any order.** The fixed wizard is replaced by an overview of the four steps (hanger distance, belt position, pen, drawing). Each one shows whether it's done and can be revisited at any time.
- **Settings survive a reboot.** The hanger distance, pen angles and the last uploaded drawing (with its preview) are kept.
- **Resume without homing.** The belt position is saved during setup, so after a power cycle you can carry on without extending to home again. A **Test move to home** checks that the position is still correct.
- Nothing is written to flash while drawing.

### Drawing
- **The web UI stays up while drawing**, showing progress, elapsed time and an estimate of the time left. You can turn this off on the Diagnostics page.
- **Pause, resume and stop.** Pausing lifts the pen after the current short move, and resume puts it back down and carries on from the same spot.
- **Renderer fixes** *(this fork)*. The rasterising canvas is cleared to white first, so shifting an image down no longer traces a stray line across the top; and very long single paths are split before previewing, because Chrome's accelerated canvas stops stroking them and the preview came out blank.
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
- **WiFi association fixed** *(this fork)*. At moderate signal levels, around -66 dBm, the ESP32 gets stuck failing the WPA2 4-way handshake and never joins, even with the right password; the log shows handshake timeouts and authentication failures. Limiting the station to 802.11b rates joins reliably every time. See [nikivanov/mural#109](https://github.com/nikivanov/mural/issues/109).
- **WiFi country code is a setting** *(this fork)*. The captive portal asks for it (empty on a first boot, prefilled afterwards) and it is kept in NVS instead of being hardcoded to `US`. It decides which channels and power levels the radio may use, so it should match where Mural is installed. The Diagnostics page reports the code the driver actually applied.
- **Runs without a WiFi network** *(this fork)*. A "Use without WiFi" button in the captive portal, or `POST /hotspot`, makes Mural host its own network after a restart; the web UI is then at `http://192.168.4.1` with no router involved. A reset returns to normal WiFi mode.
- **Corrected pulley geometry** *(this fork)*. The distance between the pulley axes was measured at 85.90 mm rather than 85.00, which changes the belt tangent geometry (`d_t` 76.927 instead of 76.027). Re-home once after updating so the saved origin matches the new model.
- Serial speed is 115200 baud (upstream uses 9600, which slowed the main loop down).
- The OLED code is skipped, since v1.1 has no OLED. To use one, build with `-DMURAL_HAS_OLED=1`.
- Builds on Windows. `build.py` no longer uses Unix shell commands. If `npm install` fails in `tsc/`, run `npm install --ignore-scripts` there instead.

## Installing

The easiest way is the **[web installer](https://duncanmcqueen.github.io/mural/)**. Plug the ESP32 in over USB and flash it from desktop Chrome or Edge, with nothing to install. Prebuilt `.bin` files for esptool are on the [releases page](https://github.com/duncanmcqueen/mural/releases).

Use **Fresh install** the first time, including when coming from upstream Mural. This fork uses a different partition table (upstream's put settings where PlatformIO writes `boot_app0.bin`, so every upload could corrupt them). After that, **Update** keeps your settings and calibration.

## Building from source

This fork builds with PlatformIO, like upstream. The firmware and the web UI live in separate flash partitions:

- **Upload** flashes the firmware (`src/`).
- **Upload Filesystem Image** flashes the web UI (`data/`). Do this only when files in `data/` have changed.

Uploading the filesystem image erases the drawing stored on the device, so you'll need to choose the drawing again afterwards. Settings and calibration aren't affected.

The firmware version comes from `git describe` and is shown on the Diagnostics page.

## Making a release

Push a tag of the form `v1.1-plus.N`:

```
git tag v1.1-plus.1
git push origin v1.1-plus.1
```

The [Release workflow](.github/workflows/release.yml) builds the firmware and web UI, creates a GitHub release with the `.bin` files attached, and publishes the web installer to GitHub Pages. To build the same files locally, run `python scripts/package_release.py` with PlatformIO's Python (`~/.platformio/penv/Scripts/python`). The output goes to `dist/`.

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

## Credits

This firmware is derivative work. Please keep these credits if you fork it further.

- **[nikivanov/mural](https://github.com/nikivanov/mural)** — the original Mural hardware, firmware and [kinematic model](KinematicModel.md), by Nik Ivanov and contributors. This fork starts at upstream's `v1.1` tag (`c817cd0`).
- **[zeiphon/mural-1-plus](https://github.com/zeiphon/mural-1-plus)** — Jonathon T (zeiphon). The `Controller` replacing the fixed setup phases, NVS persistence of the setup state, the diagnostics page and RAM ring-buffer log, ramped servo movement with soft limits, pause/resume/stop with live progress, on-the-fly pen pressure, the 115200 baud change, the partition table, and the release workflow. This branch is built on his `v1.1-plus` branch; his commits are preserved unchanged in the history.
- **Changes added in this fork** (`duncanmcqueen/mural`):
  - **WiFi association fixed by forcing 802.11b** (`dbbc44a`) — at moderate signal levels (around -66 dBm) the ESP32 fails the WPA2 4-way handshake and never joins, even with the correct password, reporting handshake timeouts and auth failures. Limiting the station to 802.11b rates associates reliably every time. See [nikivanov/mural#109](https://github.com/nikivanov/mural/issues/109).
  - **WiFi country code as a setting** (`dbbc44a`) — asked for on the captive portal (empty on a first boot, prefilled afterwards) and stored in NVS, instead of a hardcoded `US`. It decides which channels and power levels the radio may use, so it has to match where Mural is installed; `/diagnostics` reports the code the driver actually applied.
  - **Quieter first boot** (`dbbc44a`) — NVS keys are checked before being read, and the static file handler is registered after the API routes, so a fresh device no longer logs a wall of "not found" errors.
  - **Hotspot mode** (`177aab0`) — adapted from upstream `e284fbd` ("standalone ap mode"): an RTC flag, consumed on the next boot, makes Mural host its own network instead of joining one.
  - **Pulley geometry correction** (`177aab0`) — upstream `7b23140`, measured on this hardware: pulley axis distance 85.00 → 85.90 mm (`d_t` 76.927).
  - **Renderer fixes** (`8b45339`) — upstream `c61d955` (white canvas before rasterising, no stray line at the top after shifting) and `fd628ce` (split paths over 500k segments so long previews render).
  - **Setup and drawing fixes** (`f134903`) — the saved belt position is now invalidated before homing/calibration moves and on any hanger-distance change (resume used to be able to restore a stale position, or silently restore the previous distance); the web server is stopped and restarted from `loop()` rather than from inside a request handler (it never came back before); the total-distance header no longer loses its last character; exceptions from drawing tasks end the run as failed instead of aborting the board; coordinates are validated before they are committed.

Licensed under [CC BY-NC 4.0](LICENSE.md), as upstream.
