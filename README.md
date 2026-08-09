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
   40 MHz, some at 80; the TFT_eSPI defaults are more conservative. Values far
   out of range do not simply fail — at 400 MHz this panel accepted writes but
   corrupted them, which looks like a stretched, glitching image rather than a
   dead display.
3. **Keep `COOKED_PIXELS 1`.** The library composes each line in a canvas-sized
   buffer and hands back finished pixels, so a line is one contiguous push under
   a single address window per frame instead of one window per opaque run. The
   sketch allocates `canvasWidth * (canvasHeight + 2)` bytes per file and falls
   back to composing lines itself if the heap cannot cover it.
4. **Shrink the GIFs.** The ILI9488 only accepts 18-bit pixels over SPI, so
   3 bytes per pixel is a hardware floor: 420x315 is ~397 KB per frame against
   ~130 KB at 240x180. Pixel count is the dominant cost.

DMA (`USE_DMA 1`) transfers one line while the next is decoded, using two
alternating buffers. It is only compiled in when TFT_eSPI offers it: the library
defines `ESP32_DMA` in `Processors/TFT_eSPI_ESP32.h` *unless* the panel is in
18-bit mode, and `User_Setup_Select.h` puts the ILI9488 in 18-bit mode, so DMA is
live on an ST7735 and silently absent on the ILI9488.

## 1.8" 128x160 ST7735 instead

[`User_Setup_ST7735.h`](User_Setup_ST7735.h) replaces the whole contents of
`TFT_eSPI/User_Setup.h` for the smaller panel; the display keeps the same pins
(SCLK 18, MOSI 23, CS 14, DC 27, RST 22) and the card side does not change. It is
a large speed win: 128x160 at 2 bytes per pixel is ~40 KB per frame against
~397 KB for a 420x315 frame on the ILI9488, and DMA becomes available.

Two things to know: the sketch centres and clips rather than scaling, so GIFs have
to be 160x128 or smaller, and the ST7735 tab variant (`ST7735_REDTAB` and the
alternatives listed in the file) has to be matched to the board by eye - the wrong
one shows a stripe of noise along one edge or inverted colours.

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
