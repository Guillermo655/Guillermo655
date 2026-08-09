// Standalone ST7735 bring-up test: raw SPI, no TFT_eSPI, no SD, no GIF decoding.
//
// Point of this sketch: a lit but blank white panel means the controller never
// received a valid init sequence, and TFT_eSPI cannot tell us whether that is the
// wiring, the panel, or the wrong ST7735 variant. This drives the panel directly
// with the documented ST7735 startup, then cycles red, green, blue, white, black
// full-screen fills - and it tries each of the three memory offsets in turn, so a
// board that only answers with one of them still shows something.
//
// Wiring, matching the blue "1.8" TFT 128*RGB*160 VER 1.0" silkscreen:
//
//   BL  -> 3V3        SDA -> 23   (MOSI)
//   CS  -> 14         SCK -> 18
//   DC  -> 27         VCC -> 5V   (this board has an onboard regulator, U1)
//   RES -> 22         GND -> GND
//
// If every fill stays white here, the module is not responding at all and no
// software setting will change that.

#include <SPI.h>

#define PIN_CS   14
#define PIN_DC   27
#define PIN_RST  22
#define PIN_SCLK 18
#define PIN_MOSI 23

// Deliberately slow: this is a bring-up test, not a benchmark, and a marginal
// wire or a long jumper is far more likely to work at 4 MHz than at 27.
static const uint32_t SPI_HZ = 4000000;

// ST7735 commands used here.
#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT  0x11
#define ST7735_FRMCTR1 0xB1
#define ST7735_INVOFF  0x20
#define ST7735_PWCTR1  0xC0
#define ST7735_PWCTR2  0xC1
#define ST7735_PWCTR3  0xC2
#define ST7735_VMCTR1  0xC5
#define ST7735_MADCTL  0x36
#define ST7735_COLMOD  0x3A
#define ST7735_CASET   0x2A
#define ST7735_RASET   0x2B
#define ST7735_RAMWR   0x2C
#define ST7735_NORON   0x13
#define ST7735_DISPON  0x29

#define PANEL_W 128
#define PANEL_H 160

static void writeCommand(uint8_t cmd) {
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(cmd);
  digitalWrite(PIN_CS, HIGH);
}

static void writeData(const uint8_t *data, size_t len) {
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  for (size_t i = 0; i < len; i++) SPI.transfer(data[i]);
  digitalWrite(PIN_CS, HIGH);
}

static void writeData8(uint8_t d) { writeData(&d, 1); }

static void hardReset() {
  digitalWrite(PIN_RST, HIGH); delay(50);
  digitalWrite(PIN_RST, LOW);  delay(50);
  digitalWrite(PIN_RST, HIGH); delay(150);
}

// The minimum sequence that gets an ST7735/ST7735S out of sleep and into 16-bit
// colour with the display on. Panel-specific gamma and voltage tuning is left
// out: it affects how the colours look, not whether anything appears.
static void initPanel() {
  hardReset();

  writeCommand(ST7735_SWRESET); delay(150);
  writeCommand(ST7735_SLPOUT);  delay(500);

  writeCommand(ST7735_FRMCTR1);
  { const uint8_t d[] = {0x01, 0x2C, 0x2D}; writeData(d, sizeof(d)); }

  writeCommand(ST7735_PWCTR1);
  { const uint8_t d[] = {0xA2, 0x02, 0x84}; writeData(d, sizeof(d)); }
  writeCommand(ST7735_PWCTR2); writeData8(0xC5);
  writeCommand(ST7735_PWCTR3);
  { const uint8_t d[] = {0x0A, 0x00}; writeData(d, sizeof(d)); }
  writeCommand(ST7735_VMCTR1); writeData8(0x0E);

  writeCommand(ST7735_INVOFF);
  writeCommand(ST7735_MADCTL); writeData8(0x00);   // portrait, RGB order
  writeCommand(ST7735_COLMOD); writeData8(0x05);   // 16 bits per pixel

  writeCommand(ST7735_NORON);  delay(10);
  writeCommand(ST7735_DISPON); delay(100);
}

// Fills the whole panel, offsetting the address window by the amount a given
// board variant needs. The three ST7735 variants differ only in this offset, so
// trying all of them costs nothing and identifies the board.
static void fillScreen(uint16_t colour, uint8_t xOff, uint8_t yOff) {
  uint8_t caset[] = {0, (uint8_t)xOff, 0, (uint8_t)(xOff + PANEL_W - 1)};
  uint8_t raset[] = {0, (uint8_t)yOff, 0, (uint8_t)(yOff + PANEL_H - 1)};

  writeCommand(ST7735_CASET); writeData(caset, sizeof(caset));
  writeCommand(ST7735_RASET); writeData(raset, sizeof(raset));
  writeCommand(ST7735_RAMWR);

  uint8_t hi = colour >> 8, lo = colour & 0xFF;
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  for (uint32_t i = 0; i < (uint32_t)PANEL_W * PANEL_H; i++) {
    SPI.transfer(hi);
    SPI.transfer(lo);
  }
  digitalWrite(PIN_CS, HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ST7735 RAW TEST ===");
  Serial.printf("CS=%d DC=%d RST=%d SCLK=%d MOSI=%d at %lu Hz\n",
                PIN_CS, PIN_DC, PIN_RST, PIN_SCLK, PIN_MOSI,
                (unsigned long)SPI_HZ);

  pinMode(PIN_CS, OUTPUT);  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);  digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, HIGH);

  SPI.begin(PIN_SCLK, -1, PIN_MOSI, -1);
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));

  initPanel();
  Serial.println("init sequence sent");
}

void loop() {
  struct { const char *name; uint8_t x, y; } variants[] = {
    {"no offset (REDTAB)", 0, 0},
    {"2,1 offset (GREENTAB)", 2, 1},
    {"2,3 offset (GREENTAB3)", 2, 3},
  };
  struct { const char *name; uint16_t colour; } colours[] = {
    {"red", 0xF800}, {"green", 0x07E0}, {"blue", 0x001F},
    {"white", 0xFFFF}, {"black", 0x0000},
  };

  for (auto &v : variants) {
    Serial.printf("--- %s\n", v.name);
    for (auto &c : colours) {
      Serial.printf("    %s\n", c.name);
      fillScreen(c.colour, v.x, v.y);
      delay(1500);
    }
  }
}
