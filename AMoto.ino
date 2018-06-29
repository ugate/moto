/*
 Motorcycle tail light animations
 WS2812B 144 LEDS/Meter IP65/IP67/IP68 (1 meter strip)
 LM2596 Mini 360 DC/DC Buck/Step-down converter (12v -> 5v x 5pcs, 1x running light/power, 1x brake, 1x left turn, 1x right turn)
 DFMO00743 - Motorcycle Tail Light LED Integrated Brake Smoke Turn Signal License Plate (1pc)
 ESP8266 - ESP8266 NodeMCU WiFi Module (1pc)
 */

// ----------------- ESP8266 Server -----------------------------

#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <FS.h>

#define NET_NEEDS_SETUP 1 << 1
#define DNS_PORT 53                                         // the port used for DNS (AP Mode only)
#define SSID_FILE "/ssid.txt"                               // credential storage for SSID
const char* domain = "motomoon.lighting";                   // when present, the webserver will be ran with SSID as an Access Point Webserver (AP Mode), otherwise SSID as the usernamen to connect to an existing AP (Station Mode)
const char* ssidDefault = "Motomoon";                       // SSID to generated AP (AP Mode) or existing network SSID (Station Mode)
const char* passwordDefault = "motomoon";                   // password to generated AP (AP Mode) or existing network password (Station Mode)
volatile byte net_flags = 0;                                // flags for tracking web configuration state
char* ssid;                                                 // the SSID/username
char* password;                                             // the SSID password
const IPAddress ip(192, 168, 1, 1);                         // IP address to use when in AP mode (blank domain) 
DNSServer dnsServer;                                        // the DNS server instance (AP mode only)
ESP8266WebServer server(80);                                // the web server instance with port

// ----------------- FASTLED ------------------------ 

// default retry count = 0 causes flickering on ESP8266
#define FASTLED_INTERRUPT_RETRY_COUNT 0
#include <FastLED.h>

#define DETECT_MILLIS 1000        // rate which turn animations will need to ignore sudden drops due to external relays (delays brake/turn signal response rate)
#define PIN_BRAKE 0               // the pin the brake light is attached to
#define PIN_TURN_LEFT 4           // the pin the left turn signal is attached to
#define PIN_TURN_RIGHT 5          // the pin the right turn signal is attached to
#define PIN_LEDS 2                // the pin that the LED strips are attached to
#define COLOR_ORDER GRB           // LED color order of the strip
#define CHIPSET WS2812B           // LED strip chipset
#define BRIGHTNESS 192            // initial global brightness of LEDs
#define BPM 50                    // beats/minute rate in which LED animations will execute
#define COLS 19                   // the number of columns in the LED matrix
#define ROWS 3                    // the number of rows in the LED matrix
#define COL_CENTER_INDEX 9        // the index where the center LEDs are physically present between the two sections of the left/right turn signals
#define MATRIX_SERPENTINE true    // LED matrix layouts: true = odd rows left -> right, even rows left -> right (false = all rows left -> right)

#define FPS_ANIM_MILLIS round(1000 / BPM)
#define NUM_LEDS (COLS * ROWS)
#define NUM_CENTER_COLS 1
#define NUM_RIGHT_COLS (COLS - COL_CENTER_INDEX - NUM_CENTER_COLS)
#define NUM_LEFT_COLS (COLS - NUM_RIGHT_COLS - NUM_CENTER_COLS)
#define MAX_DIMENSIONS ((COLS > ROWS) ? COLS : ROWS)
#define SETUP_STAT_NONE 0
#define SETUP_STAT_INIT 1
#define SETUP_STAT_LED_COMPLETE 2
#define SETUP_STAT_NET_PENDING 3
#define SETUP_STAT_COMPLETE 4
#define ANIM_NOISE 1 << 1
#define ANIM_FIRE 1 << 2
#define ANIM_RAIN 1 << 3
#define ANIM_JUGGLE 1 << 4
#define ANIM_CONFETTI 1 << 5
#define ANIM_GLITTER 1 << 6
#define ANIM_ERROR 1 << 7
#define TURN_RISING 1 << 0
#define TURN_STAY_LIT_BACKWARDS 1 << 1
#define TURN_CYLON 1 << 2
#define RUN_ON 1 << 0
#define RUN_OFF 1 << 1
#define BRAKE_ON 1 << 2
#define BRAKE_OFF 1 << 3
#define LEFT_ON 1 << 4
#define LEFT_OFF 1 << 5
#define RIGHT_ON 1 << 6
#define RIGHT_OFF 1 << 7
volatile byte led_flags = 0;                              // flags for tracking LED states (separate ON/OFF flags for each state should exist to account for spurts of false-positive irregularities)
unsigned long brakeOffMillisPrev = 0;                     // used to avoid false-positives from possible fluctuations from external 12v relay braking input
unsigned long leftOffMillisPrev = 0;                      // used to avoid false-positives from possible fluctuations from external 12v relay left turn signal input
unsigned long rightOffMillisPrev = 0;                     // used to avoid false-positives from possible fluctuations from external 12v relay right turn signal input
unsigned long data_millis = 0;                            // general
uint8_t run_anim_flags = ANIM_RAIN;                      // animations that will be applied for running lamps ::WEB_CONFIGURABLE::
uint8_t run_speed = 8;                                    // animation speed for running lamps (may only be applicable for certain animations) ::WEB_CONFIGURABLE::
uint8_t run_scale = 100;                                  // animation scale for running lamps (may only be applicable for certain animations) ::WEB_CONFIGURABLE::
bool run_color_cycle = false;                             // animation on/off switch color cycle for running lamps (may only be applicable for certain animations) ::WEB_CONFIGURABLE::
uint8_t brake_color_index = 0;                            // palette color index for the brake light (rotates through palette)
uint8_t turn_bpm = BPM;                                   // beats/minute for turn signals ::WEB_CONFIGURABLE::
uint8_t turn_fade = 0;                                    // rate at which the turn signals fade to black: 8 bit, 1 = slow, 255 = fast, 0 = immediate ::WEB_CONFIGURABLE::
uint8_t turn_left_color_index = 0;                        // palette color index for the left turn signal (rotates through palette)
uint8_t turn_right_color_index = 0;                       // palette color index for the left turn signal (rotates through palette)
uint16_t data16_x = 0;                                    // general data usage for animations (shared between animations in order to save on memory)
uint16_t data16_y = 0;
uint16_t data16_z = 0;
uint8_t data8_x = 0;
uint8_t data8_matrix[MAX_DIMENSIONS][MAX_DIMENSIONS];
struct Pulse {
  uint8_t delta; uint16_t xy; uint16_t beat; uint16_t upper; uint16_t lower;
};
Pulse turn_left = { TURN_CYLON, 0, 0, 0, 0 };             // left turn signal animation state tracking
Pulse turn_right = { TURN_CYLON, 0, 0, 0, 0 };            // right turn signal animation state tracking
CRGB leds_plus_safety_pixel[NUM_LEDS + 1];
CRGB* const leds(leds_plus_safety_pixel + 1);
extern const TProgmemPalette16 RunPalette_p FL_PROGMEM = { // running lamp static color palette which is stored in PROGMEM (flash), which is almost always more plentiful than RAM (64 bytes of flash) ::WEB_CONFIGURABLE::
  0x000000, 0x800000, 0x000000, 0x800000,
  0x8B0000, 0x800000, 0x8B0000, 0x8B0000,
  0x8B0000, 0xFF0000, 0xFFA500, 0xFFFF33,
  0xFFA500, 0xFF0000, 0x8B0000, 0xBF360C
};
extern const TProgmemPalette16 TurnPalette_p FL_PROGMEM = { // turn signal static color palette which is stored in PROGMEM (flash), which is almost always more plentiful than RAM (64 bytes of flash) ::WEB_CONFIGURABLE::
  0xFFFF00, 0xFF6D00, 0x76FF03, 0xFF3D00,
  0xFFFF00, 0xFF6D00, 0x76FF03, 0xFF3D00,
  0xFFFF00, 0xFF6D00, 0x76FF03, 0xFF3D00,
  0xFFFF00, 0xFF6D00, 0x76FF03, 0xFF3D00
};

// ----------------- Program ------------------------ 

void loop() {
  ledLoop();
  netLoop();
}

// LED fills for visual indication of server connection statuses
void statusIndicator(const uint8_t stat, const uint8_t netStat = WL_CONNECTED, const char* ipLoc = "") {
  if (stat == SETUP_STAT_INIT) { // turn on on-board LED indicating startup is in progress
    pinMode(LED_BUILTIN, OUTPUT); // on-board LED
    digitalWrite(LED_BUILTIN, LOW); Serial.println("---> STARTING <---");
    return;
  } else if (stat == SETUP_STAT_COMPLETE) {
    FastLED.clear();
    FastLED.show();
    digitalWrite(LED_BUILTIN, HIGH); Serial.println("---> READY <---");
    return;
  }
  CRGB rgb = CRGB::Black;
  if (stat == SETUP_STAT_LED_COMPLETE) {
    rgb = CRGB::DarkSlateGray; Serial.println("LED setup complete");
  } else if (netStat == WL_CONNECTED && ipLoc != "") {
    rgb = CRGB::Green; Serial.printf("Setup complete. Web access available at IP: %s\n", ipLoc);
  } else if (stat != SETUP_STAT_LED_COMPLETE) {
    switch (netStat) {
      case WL_IDLE_STATUS:
        rgb = CRGB::Blue; Serial.println("WiFi Idle...");
        break;
      case WL_SCAN_COMPLETED:
        rgb = CRGB::Indigo; Serial.println("WiFi Scan completed...");
        break;
      case WL_NO_SSID_AVAIL:
        rgb = CRGB::DarkOrange; Serial.printf("WiFi No SSID Available for: %s\n", ssid);
        break;
      case WL_CONNECTED:
        rgb = CRGB::GreenYellow; Serial.printf("WiFi Connected to: %s\n", ssid); WiFi.printDiag(Serial);
        break;
      case WL_CONNECT_FAILED:
        rgb = CRGB::Red; Serial.printf("WiFi Connection Failed for SSID: %s\n", ssid);
        break;
      case WL_CONNECTION_LOST:
        rgb = CRGB::Magenta; Serial.printf("WiFi Connection lost for SSID: %s\n", ssid);
        break;
      case WL_DISCONNECTED:
        rgb = CRGB::Yellow; Serial.printf("WiFi Disconnected from: %s\n", ssid);
        break;
    }
  }
  fill_solid(leds, NUM_LEDS, rgb);
  FastLED.show();
}

// ----------------- General Setup ------------------------ 

void setup() {
  //system_update_cpu_freq(160); // 80 MHz or 160 MHz, default is 80 MHz
  Serial.begin(115200);
  statusIndicator(SETUP_STAT_INIT);
  //delay(1000); // ESP8266 init delay
  
  ledSetup();
  netSetup();

  // demo
  //turnLeftOn();
  //turnRightOn();

  statusIndicator(SETUP_STAT_COMPLETE);
}
