# ESP32 GIF Player (ILI9488 TFT + SD card)

Plays animated GIFs from an SD card on a 480x320 ILI9488 SPI display, with two
push buttons to cycle through files.

## Hardware

The card gets its own SPI bus (`SD_DEDICATED_BUS 1`, the default): on ESP32 it
runs on HSPI while TFT_eSPI keeps VSPI. Sharing one bus with the display did not
work here — the card never got past CMD0.

| Signal        | ESP32 pin |
| ------------- | --------- |
| SD SCK        | 25        |
| SD MISO       | 21        |
| SD MOSI       | 26        |
| SD chip select| 4         |
| Next button   | 32 (to GND, internal pull-up) |
| Prev button   | 33 (to GND, internal pull-up) |

The display's SCK/MOSI/CS/DC/RST pins are configured in TFT_eSPI's
`User_Setup.h`, not in the sketch, and none of them are shared with the card.

Setting `SD_DEDICATED_BUS` to `0` selects the old shared-bus wiring (SCK 18,
MISO 19, MOSI 23, SD CS 15) for reference. That path needs
`SUPPORT_TRANSACTIONS` in the TFT_eSPI setup, is slower, and puts the card's chip
select on GPIO15, which is a boot strapping pin.

## Playback speed

If frames visibly wipe down the screen, the display is spending its time on bus
overhead rather than pixels.

1. **Keep `SD_DEDICATED_BUS 1`.** It lets the sketch claim the display's chip
   select once per frame instead of once per pixel run. On a shared bus that
   per-frame claim is *not* safe — `gif.playFrame()` reads the card between
   scanlines, so holding the display selected across those reads drives two
   devices at once — which is why the sketch keys the locking off the same flag
   as the pin choice.
2. **Raise `SPI_FREQUENCY` in `User_Setup.h`.** ILI9488 panels usually run at
   40 MHz; the TFT_eSPI defaults are more conservative.
3. **DMA (`pushPixelsDMA`).** Overlaps the transfer of one line with decoding of
   the next, but needs alternating line buffers to avoid overwriting a buffer
   mid-transfer. Not implemented here.

## Troubleshooting

[`SdCardTest/SdCardTest.ino`](SdCardTest/SdCardTest.ino) is a standalone card
check: no display, no GIF decoding. It reports the MISO idle level, bit-bangs
CMD0 and prints the raw response (`01` = card alive in SPI mode, all `FF` =
nothing driving MISO), then mounts the card and lists the root.

[`DisplayTest/DisplayTest.ino`](DisplayTest/DisplayTest.ino) is the same idea for
the panel: no card, no decoding. It draws a border, a solid fill, colour bars and
odd-length runs through the same `setAddrWindow()`/`pushPixels()` path the player
uses, and prints what each pattern should look like, which separates address
window and byte order faults from anything the GIF decoder does.

Setting `DEBUG_FRAMES` to `1` in `GifPlayer.ino` logs every frame's rectangle,
disposal method and transparency flag along with its decode+draw time, which is
how to tell a bandwidth limit from a rendering bug.

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
