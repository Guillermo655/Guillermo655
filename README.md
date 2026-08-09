# ESP32 GIF Player (ILI9488 TFT + SD card)

Plays animated GIFs from an SD card on a 480x320 ILI9488 SPI display, with two
push buttons to cycle through files.

## Hardware

Default wiring — display and SD card on one shared SPI bus (`SD_DEDICATED_BUS 0`):

| Signal        | ESP32 pin |
| ------------- | --------- |
| SPI SCK       | 18        |
| SPI MISO      | 19        |
| SPI MOSI      | 23        |
| SD chip select| 15        |
| Next button   | 32 (to GND, internal pull-up) |
| Prev button   | 33 (to GND, internal pull-up) |

The TFT chip-select / DC / RST pins are configured in TFT_eSPI's
`User_Setup.h`, not in the sketch. On the shared bus `SUPPORT_TRANSACTIONS` must
be enabled in the TFT_eSPI setup (it is on by default for ESP32).

## Playback speed

If frames visibly wipe down the screen, the display is spending its time on bus
overhead rather than pixels. In order of effect:

1. **Give the SD card its own bus.** Set `SD_DEDICATED_BUS` to `1` in the sketch
   and move the card's four signal wires (VCC/GND stay put):

   | SD signal | new ESP32 pin |
   | --------- | ------------- |
   | SCK       | 25 |
   | MISO      | 21 |
   | MOSI      | 26 |
   | CS        | 4  |

   The card then runs on HSPI while the display keeps VSPI, and the sketch can
   claim the display's chip select once per frame instead of once per pixel run.
   All four wires must move together. On the shared bus that per-frame claim is
   *not* safe — `gif.playFrame()` reads the card between scanlines, so holding
   the display selected across those reads drives two devices at once — which is
   why the sketch keys this off the same flag as the pin choice.
2. **Raise `SPI_FREQUENCY` in `User_Setup.h`.** ILI9488 panels usually run at
   40 MHz; the TFT_eSPI defaults are more conservative.
3. **DMA (`pushPixelsDMA`).** Overlaps the transfer of one line with decoding of
   the next, but needs alternating line buffers to avoid overwriting a buffer
   mid-transfer, and TFT_eSPI's DMA must not be used while sharing the bus with
   the SD card. Not implemented here.

## Libraries

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)
- [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF)
- `SD` and `SPI` from the ESP32 Arduino core

## SD card layout

Any `.gif` files in the card root are picked up: the sketch indexes them at boot
(up to `MAX_GIFS`) and the buttons cycle through what it found, so no particular
naming scheme is required.

For smooth playback, keep the GIF canvas at or below the display resolution
(480x320) and use a FAT32-formatted card.

## Building

```
arduino-cli compile -b esp32:esp32:esp32 GifPlayer
```

## Sketch

[`GifPlayer/GifPlayer.ino`](GifPlayer/GifPlayer.ino)
