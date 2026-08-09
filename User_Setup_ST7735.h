// TFT_eSPI configuration for this project: ESP32 + 1.8" 128x160 ST7735 panel
// ("128 x RGB x 160 ver 1.0" board), with the micro-SD card on its own SPI bus.
//
// Replace the whole contents of TFT_eSPI/User_Setup.h with this file.
//
// The display is 3.3V only and has no MISO pin, so nothing has to be shared with
// the card: the card keeps GPIO 25/21/26/4 on HSPI (see GifPlayer.ino).

#define USER_SETUP_INFO "ESP32 ST7735 128x160 + dedicated SD bus"

// ---------------------------------------------------------------- driver ----

#define ST7735_DRIVER

// Panel size in portrait. The sketch calls setRotation(1), giving 160x128.
#define TFT_WIDTH  128
#define TFT_HEIGHT 160

// The ST7735 exists in variants whose memory is offset differently and whose red
// and blue channels are sometimes swapped. Exactly one of these must be enabled,
// and which one is trial and error for a given board - the symptoms are a border
// of noise along one edge (wrong offset) or inverted colours (wrong tab):
//
//   INITB       - older "blue tab" boards
//   GREENTAB    - 128x160 with a 2,1 offset
//   GREENTAB2   - as GREENTAB, colours swapped
//   GREENTAB3   - 128x160 with a 2,3 offset
//   REDTAB      - 128x160 with no offset
//   BLACKTAB    - 128x160, no offset, colours swapped
//
// Start here; if colours look inverted try BLACKTAB, and if there is a stripe of
// noise on an edge try the GREENTAB variants.
#define ST7735_REDTAB

// ------------------------------------------------------------------ pins ----

// Same display pins as the ILI9488 was on, so only the panel itself changes.
#define TFT_MOSI 23   // board marks this SDA or SDI
#define TFT_SCLK 18   // board marks this SCK or SCL
#define TFT_CS   14
#define TFT_DC   27   // board marks this A0, DC or RS
#define TFT_RST  22   // board marks this RESET or RES

// Backlight is wired straight to 3V3, so TFT_eSPI does not drive it.
// #define TFT_BL   32
// #define TFT_BACKLIGHT_ON HIGH

// ----------------------------------------------------------------- fonts ----

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT

// ------------------------------------------------------------------ speed ----

// The ST7735 datasheet allows 15 MHz; most of these boards are stable well past
// that on short wires. Drop to 20000000 if the image shows torn or missing runs.
#define SPI_FREQUENCY       27000000

// Only used by TFT_eSPI's own read/touch helpers, neither of which this project
// uses (the panel has no MISO pin).
#define SPI_READ_FREQUENCY  16000000

// The card is on a separate SPI host, so TFT_eSPI does not need to yield the bus
// between writes. Enabling it anyway costs a little speed for nothing.
// #define SUPPORT_TRANSACTIONS
