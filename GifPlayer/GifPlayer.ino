#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <AnimatedGIF.h>

TFT_eSPI tft = TFT_eSPI();
AnimatedGIF gif;

// 1 = the card has its own SPI bus and pins (see README.md); 0 = the older
// wiring where it shares the display's bus. A dedicated bus is what makes the
// card initialise reliably, and it also lets chip select be held for a whole
// frame instead of per pixel run, which removes the visible top-to-bottom
// "wipe" during playback.
#define SD_DEDICATED_BUS 1

#define BTN_NEXT 32
#define BTN_PREV 33

#if SD_DEDICATED_BUS
  #define SD_SCK   25
  #define SD_MISO  21
  #define SD_MOSI  26
  #define SD_CS     4
  SPIClass sdSPI(HSPI);
  #define SD_SPI sdSPI
#else
  // Shared with the display: TFT_eSPI drives these same pins from User_Setup.h.
  #define SD_SCK   18
  #define SD_MISO  19
  #define SD_MOSI  23
  #define SD_CS    15
  #define SD_SPI SPI
#endif

// Widest line the display can ask us to push, in pixels.
#define MAX_LINE_PIXELS 480

// SD clock candidates, tried fastest first. Breakout modules on long jumper
// wires often fail to initialise above a few MHz.
static const uint32_t SD_CLOCKS[] = {16000000, 8000000, 4000000, 1000000};

static const unsigned long DEBOUNCE_MS = 40;

// Slowest frame rate we will honour, so a GIF with a 0 ms delay cannot starve
// button polling.
static const int MIN_FRAME_MS = 10;

// 1 = log each frame's geometry, disposal method and timing over serial.
#define DEBUG_FRAMES 0

#define MAX_GIFS 32
#define MAX_NAME_LEN 64

File gifFile;
char gifNames[MAX_GIFS][MAX_NAME_LEN];
int gifCount = 0;
int currentGifIdx = 0;
bool changeGifSignal = false;
bool gifIsOpen = false;
unsigned long nextFrameMs = 0;

// Centring offsets for the file currently open, recomputed once per file.
int xOffset = 0;
int yOffset = 0;

static uint16_t lineBuffer[MAX_LINE_PIXELS];

// Pushes one horizontal run of pixels.
//
// On a dedicated SD bus the caller claims the display for the whole frame, so
// this is just an address window plus the data. On a shared bus it has to be its
// own short transaction: gif.playFrame() reads the SD card between scanlines,
// and holding the display's chip select across those reads would put two devices
// on the bus at once.
static void pushRun(int x, int y, int len, uint16_t *pixels) {
#if !SD_DEDICATED_BUS
  tft.startWrite();
#endif
  tft.setAddrWindow(x, y, len, 1);
  tft.pushPixels(pixels, len);
#if !SD_DEDICATED_BUS
  tft.endWrite();
#endif
}

void GIFDraw(GIFDRAW *pDraw) {
  int drawX = pDraw->iX + xOffset;
  int drawY = pDraw->iY + pDraw->y + yOffset;
  if (drawX >= tft.width() || drawY >= tft.height() || drawX < 0 || drawY < 0) return;

  int iWidth = pDraw->iWidth;
  if (drawX + iWidth > tft.width()) iWidth = tft.width() - drawX;
  if (iWidth > MAX_LINE_PIXELS) iWidth = MAX_LINE_PIXELS;
  if (iWidth < 1) return;

  // pPalette is always populated by the library and is already byte-swapped for
  // SPI because begin() was called with BIG_ENDIAN_PIXELS.
  uint16_t *pPal = pDraw->pPalette;
  uint8_t *s = pDraw->pPixels;

#if DEBUG_FRAMES
  if (pDraw->y == 0)
    Serial.printf("frame x=%d y=%d w=%d h=%d disposal=%u transparent=%u\n",
                  pDraw->iX, pDraw->iY, pDraw->iWidth, pDraw->iHeight,
                  pDraw->ucDisposalMethod, pDraw->ucHasTransparency);
#endif

  if (pDraw->ucDisposalMethod == 2) {
    // "Restore to background": transparent pixels become the background colour
    // instead of leaving the previous frame visible.
    for (int x = 0; x < iWidth; x++)
      if (s[x] == pDraw->ucTransparent) s[x] = pDraw->ucBackground;
    pDraw->ucHasTransparency = 0;
  }

  if (!pDraw->ucHasTransparency) {
    for (int x = 0; x < iWidth; x++) lineBuffer[x] = pPal[s[x]];
    pushRun(drawX, drawY, iWidth, lineBuffer);
    return;
  }

  // Transparent pixels must be left untouched so the previous frame shows
  // through: draw the opaque runs only, skipping over the transparent ones.
  uint8_t ucTransparent = pDraw->ucTransparent;
  int x = 0;
  while (x < iWidth) {
    while (x < iWidth && s[x] == ucTransparent) x++;
    int runStart = x;
    int len = 0;
    while (x < iWidth && s[x] != ucTransparent) lineBuffer[len++] = pPal[s[x++]];
    if (len) pushRun(drawX + runStart, drawY, len, lineBuffer);
  }
}

void * GIFOpenFile(const char *fname, int32_t *pSize) {
  if (gifFile) gifFile.close();
  gifFile = SD.open(fname);
  if (!gifFile) return NULL;
  *pSize = gifFile.size();
  return (void *)&gifFile;
}

void GIFCloseFile(void *pHandle) {
  File *f = (File *)pHandle;
  if (f) f->close();
}

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  File *f = (File *)pFile->fHandle;
  if (!f) return 0;

  // Never consume the final byte: on the Arduino SD library, seeking after
  // reaching end-of-file stops working, and playFrame() seeks back to 0 to loop.
  if (pFile->iSize - pFile->iPos < iLen) iLen = pFile->iSize - pFile->iPos - 1;
  if (iLen <= 0) return 0;

  int32_t bytesRead = f->read(pBuf, iLen);
  if (bytesRead < 0) bytesRead = 0;
  pFile->iPos = f->position();   // keep the library's internal position in sync
  return bytesRead;
}

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  File *f = (File *)pFile->fHandle;
  if (!f) return 0;
  f->seek(iPosition);
  pFile->iPos = f->position();   // keep the library's internal position in sync
  return pFile->iPos;
}

void showFatalError(const char *msg) {
  Serial.println(msg);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextWrap(true);
  tft.setCursor(10, 10);
  tft.print(msg);
}

static bool hasGifExtension(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot && strcasecmp(dot, ".gif") == 0;
}

// Indexes the GIFs in the card root, so the playlist matches what is actually
// on the card instead of a hard-coded count and naming scheme.
void scanGifs() {
  gifCount = 0;
  File root = SD.open("/");
  if (!root) return;

  for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (!entry.isDirectory() && hasGifExtension(entry.name())) {
      // entry.name() is bare on some core versions and absolute on others.
      const char *name = entry.name();
      if (name[0] == '/') snprintf(gifNames[gifCount], MAX_NAME_LEN, "%s", name);
      else snprintf(gifNames[gifCount], MAX_NAME_LEN, "/%s", name);
      Serial.printf("Found: %s\n", gifNames[gifCount]);
      if (++gifCount >= MAX_GIFS) { entry.close(); break; }
    }
    entry.close();
  }
  root.close();
  Serial.printf("%d GIF(s) on card\n", gifCount);
}

void startNewGif(int index) {
  if (gifIsOpen) gif.close();
  gifIsOpen = false;
  changeGifSignal = false;   // cleared unconditionally: a failed open must not
                             // make loop() retry on every iteration
  tft.fillScreen(TFT_BLACK);
  if (gifCount == 0) return;

  Serial.print("Playing: "); Serial.println(gifNames[index]);

  if (gif.open(gifNames[index], GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
    // Computed once per file: the canvas size cannot change mid-file, and a
    // negative offset would make setAddrWindow() wrap the write.
    xOffset = (tft.width() - gif.getCanvasWidth()) / 2;
    yOffset = (tft.height() - gif.getCanvasHeight()) / 2;
    if (xOffset < 0) xOffset = 0;
    if (yOffset < 0) yOffset = 0;
    Serial.printf("GIF opened OK: %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    gifIsOpen = true;
    nextFrameMs = millis();
  } else {
    Serial.print("ERROR: gif.open() failed, code: ");
    Serial.println(gif.getLastError());
  }
}

bool initSD() {
  for (unsigned i = 0; i < sizeof(SD_CLOCKS) / sizeof(SD_CLOCKS[0]); i++) {
    Serial.printf("Initializing SD card at %lu Hz... ", (unsigned long)SD_CLOCKS[i]);
    if (SD.begin(SD_CS, SD_SPI, SD_CLOCKS[i])) {
      Serial.println("OK");
      return true;
    }
    Serial.println("failed");
    SD.end();
    delay(50);
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== BOOT ===");

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);

  // Order is load-bearing: the card must be brought up before tft.init().
  // Calling SPI.begin() afterwards tears down the bus TFT_eSPI just claimed
  // ("spiDetachBus(): Stopping SPI bus"), and the card then answers nothing at
  // CMD0. The delay also keeps the display's power-on current draw off the rail
  // while the card is still answering. The screen is only initialised early
  // enough to report a card failure, which is why that is reported after.
#ifdef TFT_CS
  // Park the display's chip select high before talking to the card. TFT_eSPI
  // does not configure that pin until tft.init(), so it is still floating here,
  // and a panel that reads its CS as low will drive MISO while the card is
  // trying to answer CMD0.
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  SD_SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  bool sdOk = initSD();
  delay(100);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  Serial.println("OK: tft.init() complete");

  if (!sdOk) {
    showFatalError("SD card init failed");
    while (1) delay(1000);
  }

  scanGifs();
  if (gifCount == 0) {
    showFatalError("No .gif files found on SD card");
    while (1) delay(1000);
  }

  gif.begin(BIG_ENDIAN_PIXELS);
  startNewGif(currentGifIdx);
  Serial.println("=== SETUP COMPLETE ===\n");
}

// Non-blocking, debounced falling-edge detection: holding a button no longer
// stalls playback with delay() or advances repeatedly.
bool buttonPressed(uint8_t pin, bool &lastState, unsigned long &lastChangeMs) {
  bool pressed = false;
  bool state = (digitalRead(pin) == LOW);
  unsigned long now = millis();
  if (state != lastState && now - lastChangeMs >= DEBOUNCE_MS) {
    lastChangeMs = now;
    lastState = state;
    pressed = state;
  }
  return pressed;
}

void loop() {
  static bool nextState = false, prevState = false;
  static unsigned long nextChangeMs = 0, prevChangeMs = 0;

  if (buttonPressed(BTN_NEXT, nextState, nextChangeMs)) {
    currentGifIdx = (currentGifIdx + 1) % gifCount;
    changeGifSignal = true;
  }
  if (buttonPressed(BTN_PREV, prevState, prevChangeMs)) {
    currentGifIdx = (currentGifIdx + gifCount - 1) % gifCount;
    changeGifSignal = true;
  }

  if (changeGifSignal) startNewGif(currentGifIdx);

  // Frame pacing is done here rather than by playFrame(true, ...), which would
  // block inside the library and swallow button presses.
  if (gifIsOpen && (long)(millis() - nextFrameMs) >= 0) {
    int frameDelayMs = 0;
#if DEBUG_FRAMES
    unsigned long frameStartMs = millis();
#endif
#if SD_DEDICATED_BUS
    tft.startWrite();
#endif
    int result = gif.playFrame(false, &frameDelayMs);
#if SD_DEDICATED_BUS
    tft.endWrite();
#endif
#if DEBUG_FRAMES
    Serial.printf("  decode+draw %lums, gif asks for %dms\n",
                  millis() - frameStartMs, frameDelayMs);
#endif
    if (frameDelayMs < MIN_FRAME_MS) frameDelayMs = MIN_FRAME_MS;
    nextFrameMs = millis() + frameDelayMs;

    if (result == 0) {
      gif.reset();            // last frame decoded: start the animation over
    } else if (result < 0) {
      Serial.print("ERROR: playFrame() failed, getLastError(): ");
      Serial.println(gif.getLastError());
      gif.close();
      gifIsOpen = false;
    }
  }
}
