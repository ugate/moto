// default retry count = 0 causes flickering on ESP8266
#define FASTLED_INTERRUPT_RETRY_COUNT 0
#include <FastLED.h>
// user_interface for system_update_cpu_freq
//extern "C" {
//#include "user_interface.h"
//}

#define DEBUG true                // serial output diagnostics
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
#define FPS_FIRE 15               // frames/second for the fire animation
#define DNS_PORT 53               // the port used for DNS
#define MATRIX_SERPENTINE true    // LED matrix layouts: true = odd rows left -> right, even rows left -> right (false = all rows left -> right)

#define FPS_FIRE_MILLIS round(1000 / FPS_FIRE)
#define FPS (BPM * 1)
#define FPS_ANIM_MILLIS round(1000 / FPS)
#define NUM_LEDS (COLS * ROWS)
#define NUM_RIGHT_COLS (COLS - COL_CENTER_INDEX + 1)
#define NUM_LEFT_COLS (COLS - NUM_RIGHT_COLS - 1)
#define TURN_WIDTH floor(COLS / 2)
#define SSID_FILE "/ssid.txt"
#define POS_RISING 1 << 0
#define POS_RESET 1 << 1
#define TURN_STAY_LIT_BACKWARDS 1 << 2
#define TURN_CYLON 1 << 3
#define RUN_ON 1 << 0
#define RUN_OFF 1 << 1
#define BRAKE_ON 1 << 2
#define BRAKE_OFF 1 << 3
#define LEFT_ON 1 << 4
#define LEFT_OFF 1 << 5
#define RIGHT_ON 1 << 6
#define RIGHT_OFF 1 << 7
volatile byte flags = 0;                        // flags for tracking LED states (separate ON/OFF flags for each state should exist to account for spurts of false-positive irregularities)
unsigned long brakeOffMillisPrev = 0;           // to avoid false-positives from any extranal relays
unsigned long leftOffMillisPrev = 0;            // to avoid false-positives from any extranal relays
unsigned long rightOffMillisPrev = 0;           // to avoid false-positives from any extranal relays
uint8_t turn_bpm = BPM;                         // beats/minute for turn signals ::WEB_CONFIGURABLE::
uint8_t turn_color_bpm = 30;                    // beats/minute for turn signal colors ::WEB_CONFIGURABLE::
uint8_t turn_left_color_index = 0;              // palette color index for the left turn signal (rotates through palette)
uint8_t turn_right_color_index = 0;              // palette color index for the left turn signal (rotates through palette)
uint8_t turn_fade = 0;                        // rate at which the turn signals fade to black: 8 bit, 1 = slow, 255 = fast, 0 = immediate ::WEB_CONFIGURABLE::
uint8_t animHue = 0;                            // rotating base color used by the more exotic animation patterns
struct Turn {
  uint8_t delta;
  uint16_t xy;
  uint16_t beat;
  uint16_t upper;
  uint16_t lower;
};
Turn turn_left = { TURN_CYLON, 0, 0, 0, 0 };       // left turn signal animation state tracking
Turn turn_right = { TURN_CYLON, 0, 0, 0, 0 };      // right turn signal animation state tracking
CRGB leds_plus_safety_pixel[NUM_LEDS + 1];
CRGB* const leds(leds_plus_safety_pixel + 1);
extern const TProgmemPalette16 defaultPalette_p FL_PROGMEM = { // static color palette which is stored in PROGMEM (flash), which is almost always more plentiful than RAM (64 bytes of flash) ::WEB_CONFIGURABLE::
  CRGB::DarkOrange,
  CRGB::Red,
  CRGB::Yellow,
  CRGB::MintCream,
  CRGB::DarkOrange,
  CRGB::Red,
  CRGB::Yellow,
  CRGB::MintCream,
  CRGB::DarkOrange,
  CRGB::Red,
  CRGB::Yellow,
  CRGB::MintCream,
  CRGB::DarkOrange,
  CRGB::Red,
  CRGB::Yellow,
  CRGB::MintCream
};

// ----------------- Program ------------------------ 

void loop() {
  ledify();
}

// ----------------- FASTLED ------------------------ 

// This function will return the right 'led index number' for a given set of X and Y coordinates on your matrix
// for (uint8_t x = 0; x < COLS; x++) {
//   for (uint8_t y = 0; y < ROWS; y++) {
//     leds[XY(x, y, true)] = CHSV(random8(), 255, 255);
//   }
// }
uint16_t XY(const uint16_t x, const uint16_t y, const bool sepentine = false, uint16_t numCols = COLS) {
  uint16_t i;
  if (!sepentine) {
    i = (y * numCols) + x;
  }
  if (sepentine) {
    if (y & 0x01) { // odd rows run backwards
      uint8_t reverseX = (numCols - 1) - x;
      i = (y * numCols) + reverseX;
    } else { // even rows run forwards
      i = (y * numCols) + x;
    }
  }
  return i;
}
// debounce flag check for off condition (prevents in sudden fluctuations when turning flags off)
void flagged(byte onFlag, byte offFlag, unsigned long *msp) {
  if (flags & offFlag) { // off flagged
    if (millis() - *msp > DETECT_MILLIS) {
      if (DEBUG) Serial.printf("%s turn signal off. Elapsed Time: %d\n", offFlag == LEFT_OFF ? "Left" : "Right", millis() - *msp);
      *msp = millis();
      flags &= ~onFlag; // remove on flag
    }
  }
}
// fadeToBlackBy, but with a pixel range and option for immediate (i.e. fade == 0)
void blackout(const uint16_t xy1, const uint16_t xy2, const uint8_t fade) {
  //Serial.printf("BLACKOUT %u (pixel start) %u (pixel end) %u (fade rate)\n", xy1, xy2, fade);
  for (uint16_t xy = xy1; xy <= xy2; xy++) {
    if (fade > 0) leds[xy].nscale8(fade);
    else leds[xy] = CRGB::Black;
  }
}
void blackout(const uint16_t xstart, const uint16_t width, const uint16_t ystart, const uint16_t height, const bool sepentine = false, const uint8_t fade = 0) {
  uint16_t xy = 0;
  for (uint16_t y = ystart; y < height; y++) {
    for (uint16_t x = xstart; x < width; x++) {
      xy = XY(x, y, sepentine, width);
      blackout(xy, xy, fade);
    }
  }
}
// flickering white dots in random locations
void addGlitter(const fract8 chanceOfGlitter = 80) {
  if (random8() < chanceOfGlitter) leds[random16(NUM_LEDS)] += CRGB::White;
}
// random colored speckles that blink in and fade smoothly
void confetti(uint8_t hue) {
  leds[random16(NUM_LEDS)] += CHSV(hue + random8(64), 200, 255);
}
// a colored dot sweeping back and forth, with fading trails
void sinelon(uint8_t hue) {
  leds[beatsin16(13, 0, NUM_LEDS - 1)] += CHSV(hue, 255, 192);
}
// colored stripes pulsing at a defined BPM
void stripes(CRGBPalette16& palette, uint8_t hue, const bool sepentine = false, const uint16_t xstart = 0, const uint16_t width = COLS, const uint16_t ystart = 0, const uint16_t height = ROWS) {
  uint8_t beat = beatsin8(62, 64, 255);
  uint16_t xy = 0;
  for (uint16_t y = ystart; y < height; y++) {
    for (uint16_t x = xstart; x < width; x++) {
      xy = XY(x, y, sepentine, width);
      leds[xy] = ColorFromPalette(palette, hue + (xy * 2), beat - hue + (xy * 10));
    }
  }
}
// eight colored dots, weaving in and out of sync with each other
void juggle(const uint16_t xstart = 0, const uint16_t width = COLS, const uint16_t ystart = 0, const uint16_t height = ROWS, const bool sepentine = false, const uint8_t fade = 0) {
  bool full = xstart == 0 && width == COLS && ystart == 0 && height == ROWS;
  if (full) fadeToBlackBy(leds, NUM_LEDS, fade);
  else blackout(xstart, width, ystart, height, fade);
  byte hue = 0;
  uint16_t xy = 0;
  for (byte i = 0; i < 8; i++) {
    if (full) xy = beatsin16(i + 7, 0, NUM_LEDS - 1);
    else xy = XY(beatsin16(i + 7, xstart, width - 1), beatsin16(i + 7, ystart, height - 1), sepentine);
    leds[xy] |= CHSV(hue, 200, 255);
    Serial.printf("%u) %u (xy)\n", i, xy);
    hue += 32;
  }
}
// random fire-like movement, hot (0...255) increase for brightness, cooling (0...255) increase for smaller fire
void fire(uint8_t hot = 120, uint8_t cooling = 120) {
  static uint16_t spark[COLS]; // base heat
  CRGB stack[COLS][ROWS]; // stacks that are cooler
  uint16_t hotMax = hot * ROWS;
  uint16_t hot2x = hot * 2;
  uint16_t hotHalf = round(hot / 2);
  // 1. Generate sparks to re-heat
  for (int i = 0; i < COLS; i++) {
    if (spark[i] < hot) spark[i] = random16(hot2x, hotMax);
  }
  // 2. Cool all the sparks
  for (int i = 0; i < COLS; i++) {
    spark[i] = qsub8(spark[i], random8(0, cooling));
  }
  // 3. Build the stack
  // this works on the idea that pixels are "cooler" as they get further from the spark at the bottom
  for (int i = 0; i < COLS; i++) {
    unsigned int heat = constrain(spark[i], hot / 2, hotMax);
    for (int j = ROWS - 1; j >= 0; j--) {
      // Calculate the color on the palette from how hot this pixel is
      byte index = constrain(heat, 0, hot);
      stack[i][j] = ColorFromPalette(HeatColors_p, index);
      // The next higher pixel will be "cooler", so calculate the drop
      unsigned int drop = random8(0, hot);
      if (drop > heat) heat = 0; // avoid wrap-arounds from going "negative"
      else heat -= drop;
      heat = constrain(heat, 0, hotMax);
    }
  }
  // 4. map stacks to led array
  for (int i = 0; i < COLS; i++) {
    for (int j = 0; j < ROWS; j++) {
      leds[(j * COLS) + i] = stack[i][j];
    }
  }
}
// a colored dot sweeping back and forth, with fading trails (beats/min, y-axis row, width of sweep animation, LED layout, horizontal offest, horizontal/directional flip)
Turn sweep(const CRGB& color, const uint8_t bpm, const uint16_t y, const uint16_t width = NUM_LEDS, const bool serpentine = false, const int16_t offset = 0, const bool flip = false) {
  Turn turn;
  turn.lower = (COLS * y) + offset;
  turn.upper = turn.lower + width - 1;
  turn.beat = turn.xy = beatsin16(bpm, turn.lower, turn.upper);
  if (serpentine && y & 0x01) {
    turn.xy = (NUM_LEDS - 1) - turn.xy; // need to reverse position for odd rows
    turn.lower = (NUM_LEDS - 1) - turn.lower; // reverse lower bounds
    turn.upper = (NUM_LEDS - 1) - turn.upper; // reverse upper bounds
    turn.lower ^= turn.upper; // lower/upper swap
    turn.upper ^= turn.lower;
    turn.lower ^= turn.upper;
  }
  if (flip) { // need to reverse horizontal direction
    turn.xy = (NUM_LEDS - 1) - turn.xy + (serpentine && y & 0x01 ? -offset : offset);
    turn.beat = (NUM_LEDS - 1) - turn.beat + (serpentine && y & 0x01 ? -offset : offset);
    turn.lower = (NUM_LEDS - 1) - turn.lower + (serpentine && y & 0x01 ? -offset : offset);
    turn.upper = (NUM_LEDS - 1) - turn.upper + (serpentine && y & 0x01 ? -offset : offset);
    turn.lower ^= turn.upper; // lower/upper swap
    turn.upper ^= turn.lower;
    turn.lower ^= turn.upper;
  }
  //leds[turn.xy] %= 255 - (turn.frac * 16); // (dark: 0..255 :light) 25% = 64/256
  leds[turn.xy] = color;
  return turn;
}
// same as sweep except travels in a single direction based upon delta reversal (i.e. right-to-left instead of left-to-right)
Turn turn(Turn& turned, const CRGBPalette16& palette, uint8_t& colorIndex, const uint8_t bpm, uint8_t& fadeRate, const uint16_t y, const uint16_t width = NUM_LEDS, const bool serpentine = false, const bool leftToRight = true, const int16_t offset = 0) {
  // left turn signal or right turn signal w/offset for center column/light
  const CRGB color = ColorFromPalette(palette, colorIndex, 255, NOBLEND);
  Turn turn = !leftToRight ? sweep(color, bpm, y, width, serpentine) : sweep(color, bpm, y, width, serpentine, width + 1, true);
  if (y == 0 && turn.beat != turned.beat) { // only need delta on first row since all rows are mirrored
    if (turn.beat > turned.beat) turned.delta |= POS_RISING;
    else if (turn.beat < turned.beat) turned.delta &= ~POS_RISING;
  }
  turn.delta = turned.delta; // sync every row w/pre-calculated flags from 1st row
  //Serial.printf("%u) %u (turned.beat) %u (turn.beat) %u (turn.lower) %u (turn.upper)\n", y, turned.beat, turn.beat, turn.lower, turn.upper);
  if ((!leftToRight && turn.delta & POS_RISING) /* <- left turn */ || (leftToRight && !(turn.delta & POS_RISING)) /* <- right turn */) { // backwards direction
    if (!(turn.delta & TURN_STAY_LIT_BACKWARDS)) blackout(turn.lower, turn.upper, 0);
    if ((!leftToRight && turn.beat == turn.upper) /* <- left turn */ || (leftToRight && turn.beat == turn.lower) /* <- right turn */) colorIndex++;
  } else if (turn.delta & TURN_CYLON) {
    blackout(turn.lower, turn.upper, 0);
    leds[turn.xy] = color;
  }
  if (y == 0 && turn.beat != turned.beat) { // record for next beat/xy
    turned.beat = turn.beat;
    turned.xy = turn.xy;
  }
  return turn;
}
void turnSignal(const CRGBPalette16& palette, const bool left, const bool center = false, const bool right = false) {
  //const CRGB centerColor = ColorFromPalette(LavaColors_p, turn_color_index, 255, LINEARBLEND);
  //if (left && right) fadeToBlackBy(leds, NUM_LEDS, turn_fade); // 8 bit, 1 = slow, 255 = fast
  for (uint16_t y = 0; y < ROWS; y++) {
    if (center) leds[XY(COL_CENTER_INDEX, y, MATRIX_SERPENTINE)] = CRGB::Black;
    if (left) turn(turn_left, defaultPalette_p, turn_left_color_index, turn_bpm, turn_fade, y, TURN_WIDTH, MATRIX_SERPENTINE, false); // left turn signal (all rows are mirrored)
    if (right) turn(turn_right, defaultPalette_p, turn_right_color_index, turn_bpm, turn_fade, y, TURN_WIDTH, MATRIX_SERPENTINE, true, TURN_WIDTH + 1); // right turn signal w/offset for center column/light
  }
}
void brakeOn() {
  if (DEBUG) Serial.println("Brakes ON received");
  flags |= BRAKE_ON; // add
  flags &= ~BRAKE_OFF; // remove
}
void brakeOff() {
  if (DEBUG) Serial.println("Brakes OFF received");
  flags |= BRAKE_OFF; // add
  flags &= ~BRAKE_ON; // remove
}
void turnLeftOn() {
  if (DEBUG) Serial.println("Left turn signal ON received");
  flags |= LEFT_ON; // add
}
void turnLeftOff() {
  if (DEBUG) Serial.println("Left turn signal OFF received");
  flags |= LEFT_OFF; // add
}
void turnRightOn() {
  if (DEBUG) Serial.println("Right turn signal ON received");
  flags |= RIGHT_ON; // add
}
void turnRightOff() {
  if (DEBUG) Serial.println("Right turn signal OFF received");
  flags |= RIGHT_OFF; // add
}

void ledify() {
  flagged(BRAKE_ON, BRAKE_OFF, &brakeOffMillisPrev); // check if brake is on usng time threshold
  flagged(LEFT_ON, LEFT_OFF, &leftOffMillisPrev); // check if left turn signal is on usng time threshold
  flagged(RIGHT_ON, LEFT_OFF, &rightOffMillisPrev); // check if right turn signal is on usng time threshold
  EVERY_N_MILLISECONDS(FPS_ANIM_MILLIS) {
    //memcpy8(buffer, leds, sizeof(leds)); // copy values from buffer to leds
    //memset8(leds, 0, NUM_LEDS * sizeof(CRGB)); // clear the led pixel buffer
    if (flags & BRAKE_ON) {
      // brake animation on
    } else if (!(flags & LEFT_ON && flags & RIGHT_ON)) {
      //if (flags & LEFT_ON) stripes(PartyColors_p, animHue, MATRIX_SERPENTINE, NUM_LEFT_COLS, NUM_RIGHT_COLS);
      if (!(flags & LEFT_ON) && !(flags & RIGHT_ON)) juggle(); // full screen juggle
      else if (flags & RIGHT_ON) juggle(0, NUM_LEFT_COLS, 0, ROWS, MATRIX_SERPENTINE, 128); // left screen juggle
      else juggle(NUM_LEFT_COLS + 1, NUM_RIGHT_COLS, 0, ROWS, MATRIX_SERPENTINE, 128); // right screen juggle
      /*EVERY_N_MILLISECONDS(FPS_FIRE_MILLIS) {
        fire();
        FastLED.show();
      }*/
    }
    //if (DEBUG) Serial.printf("Left turn signal is %s, Right turn signal is %s\n", flags & LEFT_ON ? "ON" : flags & LEFT_OFF ? "OFF" : "N/A", flags & RIGHT_ON ? "ON" : flags & RIGHT_OFF ? "OFF" : "N/A");
    if (flags & LEFT_ON || flags & RIGHT_ON) turnSignal(defaultPalette_p, flags & LEFT_ON, false, flags & RIGHT_ON);
    FastLED.show();
  }
  //if (DEBUG) Serial.println(LEDS.getFPS());
}

void ledSetup() {
  flags |= RUN_ON; // add
  pinMode(PIN_BRAKE, INPUT_PULLUP);
  pinMode(PIN_TURN_LEFT, INPUT_PULLUP);
  pinMode(PIN_TURN_RIGHT, INPUT_PULLUP);
  /*attachInterrupt(digitalPinToInterrupt(PIN_BRAKE), brakeOn, HIGH);
  attachInterrupt(digitalPinToInterrupt(PIN_BRAKE), brakeOff, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_TURN_LEFT), turnLeftOn, HIGH);
  attachInterrupt(digitalPinToInterrupt(PIN_TURN_LEFT), turnLeftOff, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_TURN_RIGHT), turnRightOn, HIGH);
  attachInterrupt(digitalPinToInterrupt(PIN_TURN_RIGHT), turnRightOff, FALLING);*/

  FastLED.addLeds<CHIPSET, PIN_LEDS, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);//.setDither(max_bright < 255);
  FastLED.setBrightness(BRIGHTNESS);

  if (DEBUG) Serial.println("LED setup complete");
}

// ----------------- General Setup ------------------------ 

void setup() {
  //system_update_cpu_freq(160); // 80 MHz or 160 MHz, default is 80 MHz
  if (DEBUG) {
    Serial.begin(115200);
    Serial.println("Starting...");
  }
  pinMode(LED_BUILTIN, OUTPUT); // on-board LED
  //delay(1000); // ESP8266 init delay

  ledSetup();

  // demo
  turnLeftOn();
  //turnRightOn();
}
