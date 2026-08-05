/* FLASH SETTINGS
Board: LOLIN D32
Flash Frequency: 80MHz
Partition Scheme: Minimal SPIFFS
https://www.online-utility.org/image/convert/to/XBM
*/

#include "configs.h"

#ifndef HAS_SCREEN
  #define MenuFunctions_h
  #define Display_h
#endif

#include <stdio.h>

#ifdef HAS_GPS
  #include "GpsInterface.h"
#endif

#include "Assets.h"
#include "WiFiScan.h"
#ifdef HAS_SD
  #include "SDInterface.h"
#endif
#include "Buffer.h"

#ifdef HAS_FLIPPER_LED
  #include "flipperLED.h"
#elif defined(XIAO_ESP32_S3)
  #include "xiaoLED.h"
#elif defined(MARAUDER_M5STICKC) || defined(MARAUDER_M5STICKCP2)
  #include "stickcLED.h"
#elif defined(HAS_NEOPIXEL_LED)
  #include "LedInterface.h"
#endif

#include "settings.h"
#include "CommandLine.h"
#include "lang_var.h"

#ifdef HAS_BATTERY
  #include "BatteryInterface.h"
#endif

#ifdef HAS_SCREEN
  #include "Display.h"
  #include "MenuFunctions.h"
#endif

#ifdef HAS_BUTTONS
  #include "Switches.h"
  
  #if (U_BTN >= 0)
    Switches u_btn = Switches(U_BTN, 1000, U_PULL);
  #endif
  #if (D_BTN >= 0)
    Switches d_btn = Switches(D_BTN, 1000, D_PULL);
  #endif
  #if (L_BTN >= 0)
    Switches l_btn = Switches(L_BTN, 1000, L_PULL);
  #endif
  #if (R_BTN >= 0)
    Switches r_btn = Switches(R_BTN, 1000, R_PULL);
  #endif
  #if (C_BTN >= 0)
    Switches c_btn = Switches(C_BTN, 1000, C_PULL);
  #endif

#endif

WiFiScan wifi_scan_obj;
EvilPortal evil_portal_obj;
Buffer buffer_obj;
Settings settings_obj;
CommandLine cli_obj;

#ifdef HAS_GPS
  GpsInterface gps_obj;
#endif

#ifdef HAS_BATTERY
  BatteryInterface battery_obj;
#endif

#ifdef HAS_SCREEN
  Display display_obj;
  MenuFunctions menu_function_obj;
#endif

#if defined(HAS_SD) && !defined(HAS_C5_SD)
  SDInterface sd_obj;
#endif

#ifdef MARAUDER_M5STICKC
  AXP192 axp192_obj;
#endif

#ifdef HAS_FLIPPER_LED
  flipperLED flipper_led;
#elif defined(XIAO_ESP32_S3)
  xiaoLED xiao_led;
#elif defined(MARAUDER_M5STICKC) || defined(MARAUDER_M5STICKCP2)
  stickcLED stickc_led;
#elif defined(HAS_NEOPIXEL_LED)
  LedInterface led_obj;
#endif

const String PROGMEM version_number = MARAUDER_VERSION;

#ifdef HAS_NEOPIXEL_LED
  Adafruit_NeoPixel strip = Adafruit_NeoPixel(Pixels, PIN, NEO_GRB + NEO_KHZ800);
#endif

#ifdef EXT_NEOPIXEL_PIN
  Adafruit_NeoPixel ext_strip = Adafruit_NeoPixel(EXT_NEOPIXEL_NUM, EXT_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
#endif

uint32_t currentTime  = 0;

// PWM Brightness Control
#ifdef HAS_SCREEN
  #include <Preferences.h>
  #define BL_CHANNEL 0
  #define BL_FREQ 5000
  #define BL_RESOLUTION 8
  const uint8_t BL_LEVELS[] = {26, 51, 77, 102, 128, 153, 179, 204, 230, 255};
  const uint8_t BL_NUM_LEVELS = 10;
  uint8_t bl_level_idx = 9; // default full brightness
  Preferences bl_prefs;
#endif

// Helper macros for LEDC API compatibility (2.x vs 3.x board package)
#ifdef HAS_SCREEN
  #ifndef HAS_MINI_SCREEN
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      #define BL_SETUP()       ledcAttach(TFT_BL, BL_FREQ, BL_RESOLUTION)
      #define BL_SET(duty)     ledcWrite(TFT_BL, (duty))
    #else
      #define BL_SETUP()       do { ledcSetup(BL_CHANNEL, BL_FREQ, BL_RESOLUTION); ledcAttachPin(TFT_BL, BL_CHANNEL); } while(0)
      #define BL_SET(duty)     ledcWrite(BL_CHANNEL, (duty))
    #endif
  #endif
#endif

#ifndef HAS_MINI_SCREEN
  void brightnessInit() {
    #ifdef HAS_SCREEN
      BL_SETUP();
      bl_prefs.begin("backlight", false);
      bl_level_idx = bl_prefs.getUChar("level", 9);
      if (bl_level_idx >= BL_NUM_LEVELS) bl_level_idx = 9;
      BL_SET(BL_LEVELS[bl_level_idx]);
    #endif
  }

  void brightnessCycle() {
    #ifdef HAS_SCREEN
      bl_level_idx = (bl_level_idx + 1) % BL_NUM_LEVELS;
      BL_SET(BL_LEVELS[bl_level_idx]);
      bl_prefs.putUChar("level", bl_level_idx);
      Serial.print(F("[Brightness] Level "));
      Serial.print(bl_level_idx + 1);
      Serial.print(F("/"));
      Serial.print(BL_NUM_LEVELS);
      Serial.print(F(" ("));
      Serial.print(BL_LEVELS[bl_level_idx] * 100 / 255);
      Serial.println(F("%)"));
    #endif
  }

  uint8_t getBrightnessLevel() {
    #ifdef HAS_SCREEN
      return bl_level_idx;
    #else
      return 0;
    #endif
  }

  void brightnessSave(uint8_t level) {
    #ifdef HAS_SCREEN
      if (level >= BL_NUM_LEVELS) level = BL_NUM_LEVELS - 1;
      bl_level_idx = level;
      BL_SET(BL_LEVELS[bl_level_idx]);
      bl_prefs.putUChar("level", bl_level_idx);
    #endif
  }

  void backlightOn() {
    #ifdef HAS_SCREEN
      BL_SET(BL_LEVELS[bl_level_idx]);
    #endif
  }

  void backlightOff() {
    #ifdef HAS_SCREEN
      BL_SET(0);
    #endif
  }
#else
  void backlightOn() {
    #ifdef HAS_SCREEN
      #if defined(MARAUDER_MINI) || defined(MARAUDER_MINI_V3)
        digitalWrite(TFT_BL, LOW);
      #endif
    
      #if !defined(MARAUDER_MINI) && !defined(MARAUDER_MINI_V3)
        digitalWrite(TFT_BL, HIGH);
      #endif
    #endif
  }

  void backlightOff() {
    #ifdef HAS_SCREEN
      #if defined(MARAUDER_MINI) || defined(MARAUDER_MINI_V3)
        digitalWrite(TFT_BL, HIGH);
      #endif
    
      #if !defined(MARAUDER_MINI) && !defined(MARAUDER_MINI_V3)
        digitalWrite(TFT_BL, LOW);
      #endif
    #endif
  }
#endif

#ifdef HAS_C5_SD
  SPIClass sharedSPI(SPI);
  SDInterface sd_obj = SDInterface(&sharedSPI, SD_CS);
#endif

#ifdef HAS_BADGE_RETRIEVE
  extern AsyncWebServer server;  // owned by EvilPortal.cpp
  bool badge_serving = false;

  void badgeRetrieveStart() {
    wifi_scan_obj.StartScan(WIFI_SCAN_OFF);  // frees the radio from monitor mode

    static bool routes_registered = false;
    if (!routes_registered) {  // AsyncWebServer can't unregister, so only ever once
      routes_registered = true;
      server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        File root = SD.open("/");
        if (!root || !root.isDirectory()) {  // card is unreliable; don't fault the server task
          request->send(200, "text/html", F("<h2>Marauder badge</h2><p>No SD card mounted.</p>"));
          return;
        }
        String html = F("<!doctype html><meta name=viewport content='width=device-width'>"
                        "<h2>Marauder badge</h2><ul>");
        for (File f = root.openNextFile(); f; f = root.openNextFile()) {
          if (f.isDirectory()) continue;
          String name = f.name();
          if (!name.startsWith("/")) name = "/" + name;
          html += "<li><a href=\"/dl" + name + "\">" + name + "</a> " + f.size() + " bytes</li>";
        }
        html += F("</ul>");
        request->send(200, "text/html", html);
      });
      server.serveStatic("/dl", SD, "/");
    }

    WiFi.mode(WIFI_AP);
    WiFi.softAP(BADGE_AP_SSID, BADGE_AP_PASS);
    server.begin();
    badge_serving = true;
    Serial.print(F("Serving captures on "));
    Serial.print(BADGE_AP_SSID);
    Serial.print(F(" at http://"));
    Serial.println(WiFi.softAPIP());
  }

  void badgeRetrieveStop() {
    server.end();
    WiFi.softAPdisconnect(true);
    // No WiFi.mode(WIFI_OFF) here: RunRawScan re-inits the driver itself, and
    // deinitialising first only adds a state transition that could fail to recover.
    badge_serving = false;
    wifi_scan_obj.StartScan(WIFI_SCAN_RAW_CAPTURE, TFT_WHITE);
  }
#endif

void setup()
{
  randomSeed(esp_random());
  
  #ifndef DEVELOPER
    esp_log_level_set("*", ESP_LOG_NONE);
  #endif
  
  #ifndef HAS_IDF_3
    esp_spiram_init();
  #endif

  Serial.begin(115200);

  while(!Serial)
    delay(10);

  #ifdef HAS_C5_SD
    sharedSPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    delay(100);
  #endif

  #ifdef defined(MARAUDER_M5STICKC) && !defined(MARAUDER_M5STICKCP2)
    axp192_obj.begin();
  #endif

  #if defined(MARAUDER_M5STICKCP2) // Prevent StickCP2 from turning off when disconnect USB cable
    pinMode(POWER_HOLD_PIN, OUTPUT);
    digitalWrite(POWER_HOLD_PIN, HIGH);
  #endif
  
  #ifdef HAS_SCREEN
    pinMode(TFT_BL, OUTPUT);
  #endif
  
  backlightOff();
  #if BATTERY_ANALOG_ON == 1
    pinMode(BATTERY_PIN, OUTPUT);
    pinMode(CHARGING_PIN, INPUT);
  #endif
  
  // Preset SPI CS pins to avoid bus conflicts
  #ifdef HAS_SCREEN
    digitalWrite(TFT_CS, HIGH);
  #endif
  
  #if defined(HAS_SD) && !defined(HAS_C5_SD)
    pinMode(SD_CS, OUTPUT);

    delay(10);
  
    digitalWrite(SD_CS, HIGH);

    delay(10);
  #endif

  //Serial.begin(115200);

  //while(!Serial)
  //  delay(10);

  Serial.println("ESP-IDF version is: " + String(esp_get_idf_version()));

  #ifdef HAS_PSRAM
    if (!psramInit()) {
      Serial.println(F("PSRAM not available"));
    }
  #endif

  #ifdef HAS_SIMPLEX_DISPLAY
    #if defined(HAS_SD)
      // Do some SD stuff
      if(!sd_obj.initSD())
        Serial.println(F("SD Card NOT Supported"));

    #endif
  #endif

  // Adafruit Reverse TFT Feather: enable the FET-gated power rail BEFORE
  // initializing the TFT / onboard NeoPixel. (Generic FQBN = no variant auto-init.)
  #ifdef TFT_I2C_POWER
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
  #endif
  #ifdef NEOPIXEL_POWER
    pinMode(NEOPIXEL_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_POWER, HIGH);
  #endif
  #if defined(TFT_I2C_POWER) || defined(NEOPIXEL_POWER)
    delay(10); // let the rail settle
  #endif

  // Before the TFT/SD init, so a later hang can't be misread as a wiring fault.
  #ifdef EXT_NEOPIXEL_PIN
    led_obj.extSetup();
  #endif

  #ifdef HAS_SCREEN
    display_obj.RunSetup();
    display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
  #endif

  // Init PWM brightness AFTER display init (so ledcAttach overrides TFT_eSPI's pinMode)
  #ifndef HAS_MINI_SCREEN
    brightnessInit();
    backlightOff();
  #endif

  #ifdef HAS_SCREEN
    #if !defined(MARAUDER_CARDPUTER) && !defined(MARAUDER_CARDPUTER_ADV)
      display_obj.tft.drawCentreString("ESP32 Marauder", TFT_WIDTH/2, TFT_HEIGHT * 0.33, 1);
      display_obj.tft.drawCentreString("JustCallMeKoko", TFT_WIDTH/2, TFT_HEIGHT * 0.5, 1);
      display_obj.tft.drawCentreString(display_obj.version_number, TFT_WIDTH/2, TFT_HEIGHT * 0.66, 1);
    #else
      display_obj.tft.drawCentreString("ESP32 Marauder", TFT_HEIGHT/2, TFT_WIDTH * 0.33, 1);
      display_obj.tft.drawCentreString("JustCallMeKoko", TFT_HEIGHT/2, TFT_WIDTH * 0.5, 1);
      display_obj.tft.drawCentreString(display_obj.version_number, TFT_HEIGHT/2, TFT_WIDTH * 0.66, 1);
    #endif
  #endif

  backlightOn(); // Need this

  #ifdef HAS_SCREEN
    // Do some stealth mode stuff
    #ifdef HAS_BUTTONS
      if (c_btn.justPressed()) {
        display_obj.headless_mode = true;

        backlightOff();
      }
    #endif
  #endif

  settings_obj.begin();

  const char* type = settings_obj.getSettingType("ChanHop");

  if (type == nullptr || type[0] == '\0') {
    Serial.println(F("Current settings format not supported. Installing new default settings..."));
    settings_obj.createDefaultSettings(SPIFFS);
  }

  buffer_obj = Buffer();

  #ifndef HAS_SIMPLEX_DISPLAY
    #if defined(HAS_SD)
      // Do some SD stuff
      if(!sd_obj.initSD())
        Serial.println(F("SD Card NOT Supported"));

    #endif
  #endif

  wifi_scan_obj.RunSetup();

  #ifdef HAS_SCREEN
    display_obj.tft.setTextColor(TFT_GREEN, TFT_BLACK);
    display_obj.tft.drawCentreString("Initializing...", TFT_WIDTH/2, TFT_HEIGHT * 0.82, 1);
  #endif

  evil_portal_obj.setup();

  #ifdef HAS_BATTERY
    battery_obj.RunSetup();
  #endif

  #ifdef HAS_BATTERY
    battery_obj.battery_level = battery_obj.getBatteryLevel();
  #endif

  // Do some LED stuff
  #ifdef HAS_FLIPPER_LED
    flipper_led.RunSetup();
  #elif defined(XIAO_ESP32_S3)
    xiao_led.RunSetup();
  #elif defined(MARAUDER_M5STICKC)
    stickc_led.RunSetup();
  #elif defined(HAS_NEOPIXEL_LED)
    led_obj.RunSetup();
  #endif

  #ifdef HAS_GPS
    gps_obj.begin();
  #endif

  #ifdef HAS_SCREEN  
    display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
  #endif

  #ifdef HAS_SCREEN
    #if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
      display_obj.clearScreen();
    #endif
    menu_function_obj.RunSetup();
  #endif

  /*char ssidBuf[64] = {0};  // or prefill with existing SSID
  if (keyboardInput(ssidBuf, sizeof(ssidBuf), "Enter SSID")) {
    // user pressed OK
    Serial.println(ssidBuf);
  } else {
    Serial.println(F("User exited keyboard"));
  }

  menu_function_obj.changeMenu(menu_function_obj.current_menu);*/

  wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
  
  cli_obj.RunSetup();
}


void loop()
{
  currentTime = millis();

  // Scale the colour rather than calling setBrightness(), which re-quantises the
  // stored pixel values on every call and drifts; the cap set in extSetup()
  // remains the hard current limit.
  #ifdef EXT_NEOPIXEL_PIN
    static uint32_t extLedTime = 0;
    static uint32_t extLastFrames = 0;
    static uint8_t extGlow = 0;
    // Idle floor. Multiplied by EXT_NEOPIXEL_BRIGHTNESS, 128 lands near 9% of
    // full white -- the level that was confirmed visible on the ring.
    const uint16_t EXT_GLOW_FLOOR = 128;
    if (currentTime - extLedTime >= 100) {
      extLedTime = currentTime;
      uint32_t frames = wifi_scan_obj.mgmt_frames + wifi_scan_obj.data_frames;
      // StartScan zeroes these counters, so guard the wrap rather than flaring.
      uint32_t hit = frames > extLastFrames ? (frames - extLastFrames) * 40 : 0;
      extLastFrames = frames;
      if (hit > 255) hit = 255;
      uint8_t decayed = extGlow > 24 ? extGlow - 24 : 0;
      extGlow = hit > decayed ? (uint8_t)hit : decayed;
      uint16_t scale = EXT_GLOW_FLOOR + ((uint16_t)extGlow * (255 - EXT_GLOW_FLOOR)) / 255;
      uint8_t r = (EXT_NEOPIXEL_R * scale) / 255;
      uint8_t g = (EXT_NEOPIXEL_G * scale) / 255;
      uint8_t b = (EXT_NEOPIXEL_B * scale) / 255;
      #ifdef HAS_BADGE_RETRIEVE
        if (badge_serving) {  // amber is the only "not capturing" signal on a dead screen
          r = 255;
          g = 110;
          b = 0;
        }
      #endif
      led_obj.extSetColor(r, g, b);
    }
  #endif

  #ifdef HAS_BADGE_RETRIEVE
    // All three are accepted since the dead screen leaves the menu nothing to do
    // with them. Must stay above menu_function_obj: justPressed() eats the edge.
    if (d_btn.justPressed() || c_btn.justPressed() || u_btn.justPressed()) {
      if (badge_serving)
        badgeRetrieveStop();
      else
        badgeRetrieveStart();
    }
  #endif

  // Deferred out of setup() deliberately: starting a scan there panics with a
  // stack canary overflow. Run from loop() instead, after a few seconds' margin
  // for init to settle.
  #ifdef AUTO_START_RAW_SNIFF
    static bool auto_sniff_started = false;
    if (!auto_sniff_started && currentTime > 3000) {
      auto_sniff_started = true;
      wifi_scan_obj.StartScan(WIFI_SCAN_RAW_CAPTURE, TFT_WHITE);
    }
  #endif

  bool mini = false;

  #ifdef SCREEN_BUFFER
    #ifndef HAS_ILI9341
      mini = true;
    #endif
  #endif

  #if (defined(HAS_ILI9341) && !defined(MARAUDER_CYD_2USB))
    #ifdef HAS_BUTTONS
      if (c_btn.isHeld()) {
        if (menu_function_obj.disable_touch)
          menu_function_obj.disable_touch = false;
        else
          menu_function_obj.disable_touch = true;

        menu_function_obj.updateStatusBar();

        while (!c_btn.justReleased())
          delay(1);
      }
    #endif
  #endif

  // Update all of our objects
  cli_obj.main(currentTime);
  wifi_scan_obj.main(currentTime);

  #ifdef HAS_GPS
    gps_obj.main();
  #endif

  // Save buffer to SD and/or serial
  buffer_obj.save();

  #ifdef HAS_BATTERY
    battery_obj.main(currentTime);
  #endif
  if ((wifi_scan_obj.currentScanMode != WIFI_PACKET_MONITOR) ||
      (mini)) {
    #ifdef HAS_SCREEN
      menu_function_obj.main(currentTime);
    #endif
  }
  #ifdef HAS_FLIPPER_LED
    flipper_led.main();
  #elif defined(XIAO_ESP32_S3)
    xiao_led.main();
  #elif defined(MARAUDER_M5STICKC)
    stickc_led.main();
  #elif defined(HAS_NEOPIXEL_LED)
    led_obj.main(currentTime);
  #endif

  #ifdef HAS_SCREEN
    delay(1);
  #else
    delay(50);
  #endif
}
