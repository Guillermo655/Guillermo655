// Standalone display check: no SD card, no GIF decoding.
//
// Draws patterns through the same setAddrWindow()/pushPixels() path the GIF
// player uses, so skew, stray lines and colour errors can be attributed to the
// display side rather than the decoder. Each pattern holds for 3 seconds and
// what it should look like is printed over serial.
#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

#define MAX_LINE_PIXELS 480
static uint16_t lineBuffer[MAX_LINE_PIXELS];

static const unsigned long HOLD_MS = 3000;

// Raw RGB565 constants are pushed here, so the byte order has to be corrected
// for the panel. On ILI9488 (SPI_18BIT_DRIVER) TFT_eSPI inverts the sense of
// this flag: setSwapBytes(true) is what sends TFT_BLUE as blue. The GIF player
// leaves it alone because AnimatedGIF is told BIG_ENDIAN_PIXELS and hands over
// palette entries that are already swapped.
static void pushRun(int x, int y, int len, uint16_t *pixels) {
  tft.setAddrWindow(x, y, len, 1);
  tft.pushPixels(pixels, len);
}

// Solid rectangle drawn one line at a time, like a GIF frame with no
// transparency. Edges must be straight and the fill even.
void patternSolidRect(int x0, int y0, int w, int h, uint16_t colour) {
  for (int i = 0; i < w; i++) lineBuffer[i] = colour;
  tft.startWrite();
  for (int y = 0; y < h; y++) pushRun(x0, y0 + y, w, lineBuffer);
  tft.endWrite();
}

// Vertical colour bars, one line at a time: checks the palette/byte order.
void patternColourBars(int x0, int y0, int w, int h) {
  const uint16_t bars[] = {TFT_RED,  TFT_GREEN,  TFT_BLUE,  TFT_YELLOW,
                           TFT_CYAN, TFT_MAGENTA, TFT_WHITE, TFT_BLACK};
  for (int i = 0; i < w; i++) lineBuffer[i] = bars[(i * 8) / w];
  tft.startWrite();
  for (int y = 0; y < h; y++) pushRun(x0, y0 + y, w, lineBuffer);
  tft.endWrite();
}

// Odd-length runs at odd offsets, the shape the transparent-GIF path produces.
// If the display packs pixels in pairs, these are what skew.
void patternOddRuns(int x0, int y0, int w, int h) {
  tft.startWrite();
  for (int y = 0; y < h; y++) {
    int x = 0;
    int len = 1 + (y % 7);          // 1..7 pixels per run
    bool on = true;
    while (x < w) {
      if (x + len > w) len = w - x;
      if (on) {
        for (int i = 0; i < len; i++) lineBuffer[i] = TFT_WHITE;
        pushRun(x0 + x, y0 + y, len, lineBuffer);
      }
      x += len;
      on = !on;
    }
  }
  tft.endWrite();
}

// A one pixel white border around a rectangle: the fastest way to see whether
// the address window lands where it was asked to.
void patternBorder(int x0, int y0, int w, int h) {
  for (int i = 0; i < w; i++) lineBuffer[i] = TFT_WHITE;
  tft.startWrite();
  pushRun(x0, y0, w, lineBuffer);
  pushRun(x0, y0 + h - 1, w, lineBuffer);
  for (int y = 1; y < h - 1; y++) {
    pushRun(x0, y0 + y, 1, lineBuffer);
    pushRun(x0 + w - 1, y0 + y, 1, lineBuffer);
  }
  tft.endWrite();
}

void hold(const char *what) {
  Serial.println(what);
  delay(HOLD_MS);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== DISPLAY TEST ===");

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);
  Serial.printf("panel: %d x %d\n", tft.width(), tft.height());
#ifdef SPI_FREQUENCY
  Serial.printf("SPI_FREQUENCY: %d\n", SPI_FREQUENCY);
#endif
#ifdef SPI_18BIT_DRIVER
  Serial.println("SPI_18BIT_DRIVER: on (3 bytes per pixel)");
#else
  Serial.println("SPI_18BIT_DRIVER: off (2 bytes per pixel)");
#endif
}

void loop() {
  tft.fillScreen(TFT_BLACK);
  patternBorder(30, 2, 420, 315);
  hold("1: white 1px border around a 420x315 box, 30px in from the left. "
       "Bent or doubled edges = address window problem.");

  tft.fillScreen(TFT_BLACK);
  patternSolidRect(30, 2, 420, 315, TFT_BLUE);
  hold("2: even blue 420x315 box. Banding or streaks = SPI_FREQUENCY too high.");

  tft.fillScreen(TFT_BLACK);
  patternColourBars(30, 2, 420, 315);
  hold("3: red, green, blue, yellow, cyan, magenta, white, black bars in that "
       "order left to right. Wrong colours = byte order.");

  tft.fillScreen(TFT_BLACK);
  patternOddRuns(30, 2, 420, 315);
  hold("4: ragged vertical white stripes, straight and inside the box. Sheared "
       "or leaning stripes = short runs are misaligning.");

  // Whether fillScreen() actually clears matters on its own: the player relies
  // on it between files, and leftovers would look like stray bands behind a GIF.
  tft.fillScreen(TFT_BLACK);
  patternSolidRect(200, 130, 80, 60, TFT_WHITE);
  hold("5: one small white box in the middle of an otherwise black screen. "
       "Anything left over from pattern 4 means fillScreen() is not clearing.");
}
