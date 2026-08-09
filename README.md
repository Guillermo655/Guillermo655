# ESP32 GIF Player (ILI9488 TFT + SD card)

Plays animated GIFs from an SD card on a 480x320 ILI9488 SPI display, with two
push buttons to cycle through files.

## Hardware

| Signal        | ESP32 pin |
| ------------- | --------- |
| SPI SCK       | 18        |
| SPI MISO      | 19        |
| SPI MOSI      | 23        |
| SD chip select| 15        |
| Next button   | 32 (to GND, internal pull-up) |
| Prev button   | 33 (to GND, internal pull-up) |

The TFT chip-select / DC / RST pins are configured in TFT_eSPI's
`User_Setup.h`, not in the sketch. The display and the SD card share the same
SPI bus, so `SUPPORT_TRANSACTIONS` must be enabled in the TFT_eSPI setup
(it is on by default for ESP32).

## Libraries

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)
- [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF)
- `SD` and `SPI` from the ESP32 Arduino core

## SD card layout

GIFs must be in the card root, named `1.gif`, `2.gif`, `3.gif`, ... The number
of files the sketch cycles through is set by `TOTAL_GIFS` in the sketch.

For smooth playback, keep the GIF canvas at or below the display resolution
(480x320) and use a FAT32-formatted card.

## Sketch

[`GifPlayer/GifPlayer.ino`](GifPlayer/GifPlayer.ino)
