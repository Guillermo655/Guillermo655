// Standalone SD card check: no display, no GIF decoding.
//
// Use this to tell a wiring/card/power problem apart from a firmware problem.
// It parks the display's chip select high, brings the card up at descending
// clocks, and lists the card root.
#include <SPI.h>
#include <FS.h>
#include <SD.h>

// Match GifPlayer.ino. If the card has its own bus, change these to 25/21/26/4
// and set SD_DEDICATED_BUS to 1.
#define SD_DEDICATED_BUS 0
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define SD_CS    15

// The display's chip select, from TFT_eSPI's User_Setup.h. Set to -1 if no
// display is wired to this bus.
#define TFT_CS_PIN 14

#if SD_DEDICATED_BUS
SPIClass sdSPI(HSPI);
#define SD_SPI sdSPI
#else
#define SD_SPI SPI
#endif

static const uint32_t SD_CLOCKS[] = {16000000, 8000000, 4000000, 1000000, 400000};

void listRoot() {
  File root = SD.open("/");
  if (!root) {
    Serial.println("could not open /");
    return;
  }
  for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    Serial.printf("  %-32s %s%u\n", entry.name(),
                  entry.isDirectory() ? "<dir> " : "", (unsigned)entry.size());
    entry.close();
  }
  root.close();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SD CARD TEST ===");

#if TFT_CS_PIN >= 0
  pinMode(TFT_CS_PIN, OUTPUT);
  digitalWrite(TFT_CS_PIN, HIGH);   // deselect the panel: it must not drive MISO
#endif
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  SD_SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  pinMode(SD_MISO, INPUT_PULLUP);   // idle MISO should read high
  delay(5);
  Serial.printf("MISO idle level: %s (expect HIGH; LOW means the line is held "
                "down \u2014 check wiring/power)\n",
                digitalRead(SD_MISO) ? "HIGH" : "LOW");

  for (unsigned i = 0; i < sizeof(SD_CLOCKS) / sizeof(SD_CLOCKS[0]); i++) {
    Serial.printf("SD.begin() at %lu Hz... ", (unsigned long)SD_CLOCKS[i]);
    if (!SD.begin(SD_CS, SD_SPI, SD_CLOCKS[i])) {
      Serial.println("failed");
      SD.end();
      delay(50);
      continue;
    }

    Serial.println("OK");
    uint8_t type = SD.cardType();
    Serial.printf("card type: %s\n",
                  type == CARD_MMC  ? "MMC" :
                  type == CARD_SD   ? "SDSC" :
                  type == CARD_SDHC ? "SDHC/SDXC" :
                  type == CARD_NONE ? "none" : "unknown");
    Serial.printf("card size: %llu MB, used: %llu MB\n",
                  SD.cardSize() / (1024ULL * 1024ULL),
                  SD.usedBytes() / (1024ULL * 1024ULL));
    Serial.println("root listing:");
    listRoot();
    Serial.println("=== PASS ===");
    return;
  }

  Serial.println("=== FAIL: card did not initialise at any clock ===");
}

void loop() {
  delay(1000);
}
