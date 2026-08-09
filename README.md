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
