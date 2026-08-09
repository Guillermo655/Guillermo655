#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <AnimatedGIF.h>

TFT_eSPI tft = TFT_eSPI();
AnimatedGIF gif;

#define SD_CS    15
#define BTN_NEXT 32
#define BTN_PREV 33

#define SPI_SCK  18
#define SPI_MISO 19
#define SPI_MOSI 23

// Widest line the display can ask us to push, in pixels.
#define MAX_LINE_PIXELS 480

// SD clock candidates, tried fastest first. Breakout modules on long jumper
// wires often fail to initialise above a few MHz.
static const uint32_t SD_CLOCKS[] = {16000000, 8000000, 4000000, 1000000};

static const unsigned long DEBOUNCE_MS = 40;

File gifFile;
const int TOTAL_GIFS = 3;
int currentGifIdx = 1;
bool changeGifSignal = false;
bool gifIsOpen = false;

uint16_t globalPalette[256];
char filenameBuffer[64];

void GIFDraw(GIFDRAW *pDraw) {
  static uint16_t lineBuffer[MAX_LINE_PIXELS];

  // Centre the canvas, but never start off-screen when it is larger than the
  // display: a negative offset would make setAddrWindow() wrap the write.
  int x_offset = (tft.width() - gif.getCanvasWidth()) / 2;
  int y_offset = (tft.height() - gif.getCanvasHeight()) / 2;
  if (x_offset < 0) x_offset = 0;
  if (y_offset < 0) y_offset = 0;

  int drawX = pDraw->iX + x_offset;
  int drawY = pDraw->y + pDraw->iY + y_offset;
  if (drawX >= tft.width() || drawY >= tft.height() || drawY < 0) return;

  int iWidth = pDraw->iWidth;
  if (drawX + iWidth > tft.width()) iWidth = tft.width() - drawX;
  if (iWidth > MAX_LINE_PIXELS) iWidth = MAX_LINE_PIXELS;
  if (iWidth < 1) return;

  uint16_t *pPal = globalPalette;

  if (pDraw->pPalette == NULL) {
    // pPalette24 is raw RGB byte triplets: R,G,B,R,G,B,...
    for (int x = 0; x < 256; x++) {
      uint8_t r = pDraw->pPalette24[x * 3 + 0];
      uint8_t g = pDraw->pPalette24[x * 3 + 1];
      uint8_t b = pDraw->pPalette24[x * 3 + 2];
      pPal[x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
  } else {
    // Already RGB565, already byte-swapped by BIG_ENDIAN_PIXELS — use directly
    for (int x = 0; x < 256; x++) pPal[x] = pDraw->pPalette[x];
  }

  uint8_t *s = pDraw->pPixels;

  if (pDraw->ucHasTransparency) {
    uint8_t ucTransparent = pDraw->ucTransparent;
    for (int x = 0; x < iWidth; x++)
      lineBuffer[x] = (s[x] == ucTransparent) ? TFT_BLACK : pPal[s[x]];
  } else {
    for (int x = 0; x < iWidth; x++)
      lineBuffer[x] = pPal[s[x]];
  }

  tft.startWrite();
  tft.setAddrWindow(drawX, drawY, iWidth, 1);
  tft.pushPixels(lineBuffer, iWidth);
  tft.endWrite();
}

void * GIFOpenFile(const char *fname, int32_t *pSize) {
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
  tft.setCursor(10, 10);
  tft.print(msg);
}

void startNewGif(int index) {
  if (gifIsOpen) gif.close();
  gifIsOpen = false;
  changeGifSignal = false;   // cleared unconditionally: a missing file must not
                             // make loop() retry on every iteration
  tft.fillScreen(TFT_BLACK);
  snprintf(filenameBuffer, sizeof(filenameBuffer), "/%d.gif", index);
  Serial.print("Playing: "); Serial.println(filenameBuffer);

  if (!SD.exists(filenameBuffer)) {
    Serial.print("ERROR: file not found: "); Serial.println(filenameBuffer);
    return;
  }

  if (gif.open(filenameBuffer, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
    Serial.printf("GIF opened OK: %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    gifIsOpen = true;
  } else {
    Serial.print("ERROR: gif.open() failed, code: ");
    Serial.println(gif.getLastError());
  }
}

bool initSD() {
  for (unsigned i = 0; i < sizeof(SD_CLOCKS) / sizeof(SD_CLOCKS[0]); i++) {
    Serial.printf("Initializing SD card at %lu Hz... ", (unsigned long)SD_CLOCKS[i]);
    if (SD.begin(SD_CS, SPI, SD_CLOCKS[i])) {
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

  // Bring the display up first so SD failures can be reported on screen.
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  Serial.println("OK: tft.init() complete");

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS);

  if (!initSD()) {
    showFatalError("SD card init failed");
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
    currentGifIdx++;
    if (currentGifIdx > TOTAL_GIFS) currentGifIdx = 1;
    changeGifSignal = true;
  } else if (buttonPressed(BTN_PREV, prevState, prevChangeMs)) {
    currentGifIdx--;
    if (currentGifIdx < 1) currentGifIdx = TOTAL_GIFS;
    changeGifSignal = true;
  }

  if (changeGifSignal) startNewGif(currentGifIdx);

  if (gifIsOpen) {
    int result = gif.playFrame(true, NULL);
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
