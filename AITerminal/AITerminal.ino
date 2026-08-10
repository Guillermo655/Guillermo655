/*
  =============================================================================
  ESP32 NOKIA T9 AI TERMINAL (Groq / Llama 3.1)
  =============================================================================
  A pocket AI chat terminal: type with a 4x4 keypad using Nokia-style
  multi-tap (T9), send the message to the Groq API, and read the answer
  on an ILI9488 TFT display.

  ---------------------------------------------------------------------------
  1. HARDWARE MATERIALS
  ---------------------------------------------------------------------------
  - ESP32-WROOM-32 (30 or 38 pin)
  - ILI9488 3.5" TFT LCD (480x320, SPI)
  - 4x4 Matrix Membrane Keypad
  - 1x Push Button (Caps Lock)
  - Jumper Wires

  ---------------------------------------------------------------------------
  2. WIRING DIAGRAM
  ---------------------------------------------------------------------------
  Display runs in LANDSCAPE (480x320) via tft.setRotation(1).
  Use setRotation(3) if you want it rotated 180 degrees.

  ILI9488 DISPLAY:
    VCC    -> 5V or 3.3V (check your module)
    GND    -> GND
    CS     -> GPIO 15
    DC/RS  -> GPIO 2
    RST    -> GPIO 4
    SDI    -> GPIO 23 (MOSI)
    SCK    -> GPIO 18
    LED    -> GPIO 22  (backlight control -- see note below)

  BACKLIGHT NOTE:
    Most ILI9488 breakout boards have an onboard backlight driver /
    series resistor on the LED pin, so it is a logic-level input and can
    be driven straight from GPIO 22. If your board's LED pin feeds the
    LEDs directly (it will pull well over 40mA and the ESP32 pin gets
    hot / the screen is dim), drive it through a transistor instead:

      GPIO 22 --[1k]--> base of a 2N2222 NPN
      LED pin --------> collector
      GND ------------> emitter

    Or just leave LED wired to 3.3V and set BL_PIN to -1 below; the
    screen will blank on power-off but the backlight stays lit.

  4x4 MATRIX KEYPAD:
    Row 1  -> GPIO 16
    Row 2  -> GPIO 17
    Row 3  -> GPIO 32
    Row 4  -> GPIO 33
    Col 1  -> GPIO 27
    Col 2  -> GPIO 14
    Col 3  -> GPIO 26   (NOT GPIO 12! GPIO 12 is the MTDI strapping pin:
                         if it reads HIGH at boot the ESP32 sets the flash
                         voltage to 1.8V and may fail to boot.)
    Col 4  -> GPIO 13

  CAPS LOCK BUTTON:
    Pin 1  -> GPIO 25   (was GPIO 5; GPIO 25 is RTC-capable so it can also
                         wake the board from deep sleep)
    Pin 2  -> GND (uses internal pull-up)

  ---------------------------------------------------------------------------
  3. REQUIRED LIBRARIES (Library Manager)
  ---------------------------------------------------------------------------
  - TFT_eSPI       (Bodmer)
  - Keypad         (Mark Stanley / Alexander Brevig)
  - ArduinoJson    v7 or newer  (v6 will NOT compile: it lacks JsonDocument)

  ---------------------------------------------------------------------------
  4. TFT_eSPI User_Setup.h (MANDATORY)
  ---------------------------------------------------------------------------
  Go to Arduino/libraries/TFT_eSPI/User_Setup.h, delete everything,
  and paste this:

    #define USER_SETUP_ID 1
    #define ILI9488_DRIVER
    #define TFT_MISO -1
    #define TFT_MOSI 23
    #define TFT_SCLK 18
    #define TFT_CS   15
    #define TFT_DC    2
    #define TFT_RST   4
    #define LOAD_GLCD
    #define LOAD_FONT2
    #define LOAD_FONT4
    #define SMOOTH_FONT
    #define SPI_FREQUENCY 27000000

  ---------------------------------------------------------------------------
  5. API KEY
  ---------------------------------------------------------------------------
  Get a free key at https://console.groq.com and paste it into apiKey
  below. NEVER share or commit a real key -- anyone who has it can use
  your account. If a key ever leaks, revoke it immediately.

  ---------------------------------------------------------------------------
  6. OPERATING INSTRUCTIONS (NOKIA T9 MODE)
  ---------------------------------------------------------------------------
  MAIN SCREEN:
  - [1-9] : Nokia multi-tap (e.g. tap '2' three times for 'c').
  - [0]   : TAP for ' ' (space) | double tap for '0'.
  - [#]   : TAP to SEND to AI | HOLD (1s) for PERSONA MENU.
  - [*]   : TAP for '*'        | HOLD (1s) for WIFI MENU.
  - [A]   : Scroll AI text UP.
  - [B]   : Scroll AI text DOWN.
  - [C]   : Cycle symbols  " ! ? ; :
  - [D]   : TAP for backspace  | HOLD (1s) to CLEAR ALL.
  - [Btn] : Physical button (GPIO 25) toggles CAPS LOCK.
            HOLD it for 5s to POWER OFF (backlight off, panel asleep,
            ESP32 in deep sleep -- the screen goes completely dark).
            Press the same button again to turn it back on.

  MENUS:
  - Same T9 typing. [#] confirms and moves to the next step.
  - [A] cancels the menu without saving.
  - [*] types '*' and [B] types '.' (handy for SSIDs/passwords).
  - The device restarts after saving WiFi or Persona settings.
  =============================================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <Keypad.h>
#include <Preferences.h>
#include <driver/rtc_io.h>

Preferences prefs;
TFT_eSPI tft = TFT_eSPI();

// --- CONFIG ---
#define CAPS_PIN 25          // RTC-capable pin, so it can wake from deep sleep
#define HOLD_MS 1000        // long-press duration for #, * and D
#define HTTP_TIMEOUT_MS 20000
#define SCROLL_CHARS 78      // roughly one line of text at 480px wide, size 1
#define POWEROFF_MS 5000     // hold the caps button this long to power down
#define BL_PIN 22            // backlight control; set to -1 if LED is wired to 3.3V
#define BL_ON HIGH           // flip to LOW if your board's backlight is active-low
String ssid, password, aiPersona;
const char* apiKey = "YOUR_GROQ_API_KEY";   // <-- paste your key here

// --- KEYPAD ---
const byte ROWS = 4; const byte COLS = 4;
char keys[ROWS][COLS] = {{'1','2','3','A'},{'4','5','6','B'},{'7','8','9','C'},{'*','0','#','D'}};
byte rowPins[ROWS] = {16, 17, 32, 33};
byte colPins[COLS] = {27, 14, 26, 13};   // Col 3 on GPIO 26 (see wiring notes)
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- T9 SETTINGS ---
const char* t9Letters[] = {" 0", "1", "abc2", "def3", "ghi4", "jkl5", "mno6", "pqrs7", "tuv8", "wxyz9"};
const char* symbols = "\"!?;:";

// --- GLOBAL TRACKING ---
int tapCount = 0;
char lastKeyUsed = NO_KEY;
unsigned long lastPressTime = 0;
bool capsLock = false;
bool capsBtnWasDown = false;
unsigned long capsDownSince = 0;
String userQuery = "";
String lastAIResponse = "No messages.";
int scrollPos = 0;

void setup() {
  Serial.begin(115200);
  rtc_gpio_deinit((gpio_num_t)CAPS_PIN);   // release the pin after a deep-sleep wake
  pinMode(CAPS_PIN, INPUT_PULLUP);
  if (BL_PIN >= 0) { gpio_deep_sleep_hold_dis(); gpio_hold_dis((gpio_num_t)BL_PIN); }
  backlight(true);
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);   // 1 = landscape 480x320 (use 3 to flip 180)

  keypad.setHoldTime(HOLD_MS);

  prefs.begin("ai-term", false);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("pass", "");
  aiPersona = prefs.getString("persona", "a helpful assistant");
  connectToWiFi();
}

void backlight(bool on) {
  if (BL_PIN < 0) return;
  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, on ? BL_ON : !BL_ON);
}

// Blanks the panel, kills the backlight and deep-sleeps the ESP32.
// Press the caps button again to wake it back up.
void powerDown() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(150, 150); tft.setTextSize(2); tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.print("POWERING OFF");
  delay(700);
  tft.fillScreen(TFT_BLACK);
  tft.writecommand(TFT_DISPOFF);   // display off
  tft.writecommand(TFT_SLPIN);     // panel sleep
  delay(150);
  backlight(false);                // kill the backlight: screen fully dark
  if (BL_PIN >= 0) {               // keep the pin driven while asleep,
    gpio_hold_en((gpio_num_t)BL_PIN);   // otherwise it floats and the
    gpio_deep_sleep_hold_en();          // backlight can flicker back on
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Wait for the button to be let go, otherwise we'd wake up instantly.
  while (digitalRead(CAPS_PIN) == LOW) delay(20);
  delay(50);

  rtc_gpio_pullup_en((gpio_num_t)CAPS_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)CAPS_PIN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)CAPS_PIN, 0);   // wake when pulled LOW
  esp_deep_sleep_start();
}

// Edge-detected caps toggle: fires once per press, not while held.
// Holding the same button for POWEROFF_MS shuts the terminal down.
void checkCaps() {
  bool down = (digitalRead(CAPS_PIN) == LOW);
  if (down && !capsBtnWasDown) {
    capsDownSince = millis();
    capsLock = !capsLock;
    tft.fillRect(350, 5, 130, 15, TFT_BLACK);
    tft.setCursor(360, 10); tft.setTextSize(1);
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.print(capsLock ? "CAPS: ON" : "CAPS: OFF");
  }
  if (down && (millis() - capsDownSince) >= POWEROFF_MS) powerDown();
  capsBtnWasDown = down;
}

// --- THE UNIVERSAL T9 ENGINE ---
void runT9Engine(char key, String &target) {
  if (isdigit(key) || key == 'C') {
    if (key == lastKeyUsed && (millis() - lastPressTime) < 900) {
      if (target.length() > 0) target.remove(target.length() - 1);
      tapCount++;
    } else {
      tapCount = 0;
    }

    String charToAdd = "";
    if (isdigit(key)) {
      int num = key - '0';
      if (tapCount >= (int)strlen(t9Letters[num])) tapCount = 0;
      charToAdd = String(t9Letters[num][tapCount]);
    } else {
      if (tapCount >= (int)strlen(symbols)) tapCount = 0;
      charToAdd = String(symbols[tapCount]);
    }

    if (capsLock) charToAdd.toUpperCase();
    target += charToAdd;

    lastKeyUsed = key;
    lastPressTime = millis();
    updateTypingBarGeneric(target);
  }
}

void loop() {
  checkCaps();
  keypad.getKeys();   // refresh key states; we read keypad.key[0] below

  if (keypad.key[0].stateChanged) {
    char k = keypad.key[0].kchar;

    switch (keypad.key[0].kstate) {
      case PRESSED:
        if (isdigit(k) || k == 'C') runT9Engine(k, userQuery);
        else lastKeyUsed = k;
        break;

      case HOLD:
        if (k == '*') wifiMenu();
        else if (k == '#') personaMenu();
        else if (k == 'D') { userQuery = ""; updateTypingBarGeneric(userQuery); lastKeyUsed = NO_KEY; }
        break;

      case RELEASED:
        // Skip the release action if this key just performed its HOLD action.
        if (lastKeyUsed == NO_KEY) { lastKeyUsed = k; break; }
        if (k == 'D') {
          if (userQuery.length() > 0) userQuery.remove(userQuery.length() - 1);
          updateTypingBarGeneric(userQuery);
        }
        else if (k == '#') { if (userQuery.length() > 0) { askGroq(userQuery); userQuery = ""; } }
        else if (k == '*') { userQuery += "*"; updateTypingBarGeneric(userQuery); }
        else if (k == 'A') { scrollPos = max(0, scrollPos - SCROLL_CHARS); refreshUI("AI RESPONSE:", lastAIResponse); }
        else if (k == 'B') {
          scrollPos = min(scrollPos + SCROLL_CHARS, (int)lastAIResponse.length());
          refreshUI("AI RESPONSE:", lastAIResponse);
        }
        break;

      default: break;
    }
  }
  delay(5);
}

// --- MENUS (SYNCED WITH ENGINE) ---
// Returns false if the user cancelled with [A].
bool menuInput(String title, String hint, String &target) {
  scrollPos = 0;
  refreshUI(title, hint + "  ([A]=cancel)");
  updateTypingBarGeneric(target);
  while (true) {
    checkCaps();
    char k = keypad.getKey();
    if (k == '#') return true;
    if (k == 'A') return false;
    if (k != NO_KEY) {
      if (isdigit(k) || k == 'C') runT9Engine(k, target);
      if (k == 'D' && target.length() > 0) { target.remove(target.length() - 1); updateTypingBarGeneric(target); }
      if (k == '*') { target += "*"; updateTypingBarGeneric(target); }
      if (k == 'B') { target += "."; updateTypingBarGeneric(target); }  // handy for SSIDs
    }
    delay(5);
  }
}

void wifiMenu() {
  String nS = "", nP = "";
  if (!menuInput("WIFI CONFIG", "SSID then #:", nS)) { refreshUI("CANCELLED", lastAIResponse); return; }
  if (!menuInput("WIFI CONFIG", "PASS then #:", nP)) { refreshUI("CANCELLED", lastAIResponse); return; }
  prefs.putString("ssid", nS); prefs.putString("pass", nP);
  ESP.restart();
}

void personaMenu() {
  String nP = "";
  if (!menuInput("SET PERSONA", "Persona then #:", nP)) { refreshUI("CANCELLED", lastAIResponse); return; }
  prefs.putString("persona", nP);
  ESP.restart();
}

// --- UTILS ---
void updateTypingBarGeneric(String txt) {
  tft.fillRect(0, 290, 480, 30, TFT_BLACK);
  tft.setCursor(5, 300); tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.setTextSize(1);
  tft.print("> " + txt + "_");
}

void refreshUI(String title, String body) {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10); tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(2);
  tft.println(title);
  tft.setCursor(10, 45); tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setTextSize(1);
  tft.setTextWrap(true);
  if (scrollPos > (int)body.length()) scrollPos = body.length();
  tft.println(body.substring(scrollPos));
  updateTypingBarGeneric(userQuery);
}

void connectToWiFi() {
  tft.fillScreen(TFT_BLACK); tft.setCursor(10, 10); tft.println("CONNECTING...");
  if (ssid.length() == 0) { refreshUI("NO WIFI SET", "Hold * for WiFi Menu"); return; }
  WiFi.mode(WIFI_STA); WiFi.begin(ssid.c_str(), password.c_str());
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) { delay(500); tft.print("."); timeout++; }
  if (WiFi.status() == WL_CONNECTED) refreshUI("ONLINE", "Caps: Pin 5 | * Menu");
  else refreshUI("OFFLINE", "Hold * for WiFi Menu");
}

void askGroq(String prompt) {
  refreshUI("THINKING...", "...");
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(client, "api.groq.com", 443, "/openai/v1/chat/completions", true);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(apiKey));

  // Build the body with ArduinoJson so quotes/backslashes in the prompt
  // (e.g. the '"' symbol from the C key) are escaped correctly.
  JsonDocument req;
  req["model"] = "llama-3.1-8b-instant";
  req["max_tokens"] = 200;
  JsonObject msg = req["messages"].add<JsonObject>();
  msg["role"] = "user";
  msg["content"] = "Act as " + aiPersona + ". Max 80 words: " + prompt;
  String body;
  serializeJson(req, body);

  int httpCode = http.POST(body);
  if (httpCode == 200) {
    JsonDocument res;
    DeserializationError err = deserializeJson(res, http.getString());
    const char* answer = err ? nullptr : (const char*)res["choices"][0]["message"]["content"];
    if (answer) { lastAIResponse = String(answer); scrollPos = 0; refreshUI("AI RESPONSE:", lastAIResponse); }
    else { refreshUI("ERROR", "Bad JSON reply"); }
  } else {
    refreshUI("ERROR", "Code: " + String(httpCode));
  }
  http.end();
}
