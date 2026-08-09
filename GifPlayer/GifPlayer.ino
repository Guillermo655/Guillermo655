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

File gifFile;
const int TOTAL_GIFS = 3;
int currentGifIdx = 1;
bool changeGifSignal = false;
bool gifIsOpen = false;

uint16_t globalPalette[256];
char filenameBuffer[64];

void GIFDraw(GIFDRAW *pDraw) {
  static uint16_t lineBuffer[480];  // shared scratch buffer, sized once, reused every call

  uint8_t *s;
  uint16_t *pPal;
  int x, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth + pDraw->iX > tft.width()) iWidth = tft.width() - pDraw->iX;
  if (pDraw->iY + pDraw->y >= tft.height() || pDraw->iX >= tft.width() || iWidth < 1) return;

  pPal = globalPalette;

  if (pDraw->pPalette == NULL) {
    // pPalette24 is raw RGB byte triplets: R,G,B,R,G,B,...
    for (x = 0; x < 256; x++) {
      uint8_t r = pDraw->pPalette24[x * 3 + 0];
      uint8_t g = pDraw->pPalette24[x * 3 + 1];
      uint8_t b = pDraw->pPalette24[x * 3 + 2];
      pPal[x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
  } else {
    // Already RGB565, already byte-swapped by BIG_ENDIAN_PIXELS — use directly
    for (x = 0; x < 256; x++) pPal[x] = pDraw->pPalette[x];
  }

  s = pDraw->pPixels;
  int x_offset = (tft.width() - gif.getCanvasWidth()) / 2;
  int y_offset = (tft.height() - gif.getCanvasHeight()) / 2;

  tft.startWrite();
  tft.setAddrWindow(pDraw->iX + x_offset, pDraw->y + pDraw->iY + y_offset, iWidth, 1);

  if (pDraw->ucHasTransparency) {
    uint8_t ucTransparent = pDraw->ucTransparent;
    for (x = 0; x < iWidth; x++)
      lineBuffer[x] = (s[x] == ucTransparent) ? TFT_BLACK : pPal[s[x]];
  } else {
    for (x = 0; x < iWidth; x++)
      lineBuffer[x] = pPal[s[x]];
  }

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
  int32_t bytesRead = f ? f->read(pBuf, iLen) : 0;
  pFile->iPos = f->position();   // keep the library's internal position in sync
  return bytesRead;
}

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  File *f = (File *)pFile->fHandle;
  f->seek(iPosition);
  pFile->iPos = f->position();   // keep the library's internal position in sync
  return pFile->iPos;
}

void startNewGif(int index) {
  gif.close();
  gifIsOpen = false;
  tft.fillScreen(TFT_BLACK);
  memset(filenameBuffer, 0, sizeof(filenameBuffer));
  sprintf(filenameBuffer, "/%d.gif", index);
  Serial.print("Playing: "); Serial.println(filenameBuffer);

  if (!SD.exists(filenameBuffer)) {
    Serial.print("ERROR: file not found: "); Serial.println(filenameBuffer);
    return;
  }

  if (gif.open(filenameBuffer, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
    Serial.printf("GIF opened OK: %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    changeGifSignal = false;
    gifIsOpen = true;
  } else {
    Serial.print("ERROR: gif.open() failed, code: ");
    Serial.println(gif.getLastError());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== BOOT ===");

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);

  SPI.begin(18, 19, 23, SD_CS);

  Serial.print("Initializing SD card... ");
  if (!SD.begin(SD_CS, SPI, 18000000)) {
    Serial.println("FAILED");
    while (1) delay(1000);
  }
  Serial.println("OK");

  delay(100);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  Serial.println("OK: tft.init() complete");

  gif.begin(BIG_ENDIAN_PIXELS);
  startNewGif(currentGifIdx);
  Serial.println("=== SETUP COMPLETE ===\n");
}

void loop() {
  if (digitalRead(BTN_NEXT) == LOW) {
    delay(250);
    currentGifIdx++;
    if (currentGifIdx > TOTAL_GIFS) currentGifIdx = 1;
    changeGifSignal = true;
  }
  if (digitalRead(BTN_PREV) == LOW) {
    delay(250);
    currentGifIdx--;
    if (currentGifIdx < 1) currentGifIdx = TOTAL_GIFS;
    changeGifSignal = true;
  }
  if (changeGifSignal) startNewGif(currentGifIdx);

  if (gifIsOpen) {
    int result = gif.playFrame(true, NULL);
    if (result < 1) {
      Serial.print("playFrame() failed/ended, result: ");
      Serial.print(result);
      Serial.print(", getLastError(): ");
      Serial.println(gif.getLastError());
      gif.reset();
    }
  }
}
