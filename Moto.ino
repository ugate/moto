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
#define NUM_CENTER_COLS 1
#define NUM_RIGHT_COLS (COLS - COL_CENTER_INDEX - NUM_CENTER_COLS)
#define NUM_LEFT_COLS (COLS - NUM_RIGHT_COLS - NUM_CENTER_COLS)
#define MAX_DIMENSIONS ((COLS > ROWS) ? COLS : ROWS)
#define SSID_FILE "/ssid.txt"
#define ANIM_NOISE 1 << 1
#define ANIM_FIRE 1 << 2
#define ANIM_RAIN 1 << 3
#define ANIM_JUGGLE 1 << 4
#define ANIM_CONFETTI 1 << 5
#define ANIM_GLITTER 1 << 6
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
volatile byte flags = 0;                                  // flags for tracking LED states (separate ON/OFF flags for each state should exist to account for spurts of false-positive irregularities)
unsigned long brakeOffMillisPrev = 0;                     // used to avoid false-positives from possible fluctuations from external 12v relay braking input
unsigned long leftOffMillisPrev = 0;                      // used to avoid false-positives from possible fluctuations from external 12v relay left turn signal input
unsigned long rightOffMillisPrev = 0;                     // used to avoid false-positives from possible fluctuations from external 12v relay right turn signal input
uint8_t run_anim_flags = ANIM_NOISE;                      // animations that will be applied for running lamps ::WEB_CONFIGURABLE::
uint8_t run_speed = 8;                                    // animation speed for running lamps (may only be applicable for certain animations) ::WEB_CONFIGURABLE::
uint8_t run_scale = 100;                                  // animation scale for running lamps (may only be applicable for certain animations) ::WEB_CONFIGURABLE::
bool run_color_cycle = false;                             // animation on/off switch color cycle for running lamps (may only be applicable for certain animations) ::WEB_CONFIGURABLE::
uint8_t brake_color_index = 0;                            // palette color index for the brake light (rotates through palette)
uint8_t turn_bpm = BPM;                                   // beats/minute for turn signals ::WEB_CONFIGURABLE::
uint8_t turn_fade = 0;                                    // rate at which the turn signals fade to black: 8 bit, 1 = slow, 255 = fast, 0 = immediate ::WEB_CONFIGURABLE::
uint8_t turn_left_color_index = 0;                        // palette color index for the left turn signal (rotates through palette)
uint8_t turn_right_color_index = 0;                       // palette color index for the left turn signal (rotates through palette)
uint8_t noise_matrix[MAX_DIMENSIONS][MAX_DIMENSIONS];     // for tracking noise level dimensions
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
  ledify();
}

// ----------------- FASTLED ------------------------ 

// This function will return the right 'led index number' for a given set of X and Y coordinates on your matrix
// for (uint8_t x = 0; x < COLS; x++) {
//   for (uint8_t y = 0; y < ROWS; y++) {
//     leds[XY(x, y)] = CHSV(random8(), 255, 255);
//   }
// }
uint16_t XY(const uint16_t x, const uint16_t y, uint16_t numCols = COLS, const bool serpentine = MATRIX_SERPENTINE) {
  uint16_t xy;
  if (!serpentine || !(y & 0x01)) xy = (y * numCols) + x; // even rows run forwards
  else { // odd rows run backwards
    uint8_t reverseX = (numCols - 1) - x;
    xy = (y * numCols) + reverseX;
  }
  return xy;
}
// fadeToBlackBy, but with a pixel range and option for immediate (i.e. fade == 0)
void blackout(const uint16_t xy1, const uint16_t xy2, const uint8_t fade) { //Serial.printf("BLACKOUT %u (pixel start) %u (pixel end) %u (fade rate)\n", xy1, xy2, fade);
  for (uint16_t xy = xy1; xy <= xy2; xy++) {
    if (fade > 0) leds[xy].nscale8(fade);
    else leds[xy] = CRGB::Black;
  }
}
// fadeToBlackBy, but with a matrix pixel range and option for immediate (i.e. fade == 0)
void blackout(const uint16_t xstart, const uint16_t width, const uint16_t ystart, const uint16_t height, const uint8_t fade = 0, const bool serpentine = MATRIX_SERPENTINE) {
  uint16_t xy = 0;
  for (uint16_t y = ystart; y < height; y++) {
    for (uint16_t x = xstart; x < width; x++) {
      xy = XY(x, y, width, serpentine);
      blackout(xy, xy, fade);
    }
  }
}
// similar to the built-in map function except it is confined to a submatrix region that spans multiple rows on the left/right side of the display
void mapToSide(uint16_t& xy, const bool left, const bool right, const bool clearRegion = false, const bool serpentine = MATRIX_SERPENTINE) {
  if (left && right) return; // nothing to map, pixels fill the entire matrix
  for (uint16_t y = 0, xycl = 0, xycu = 0, xyl = 0, xyu = 0; y < ROWS; y++) {
    xycl = COLS * y;
    xycu = xycl + COLS - 1;
    if (clearRegion) blackout(xycl, xycu, 0);
    if (xy >= xycl && xy <= xycu) { // xy falls within row
      xyl = left ? COLS * y : NUM_LEFT_COLS * (y + 1) + NUM_CENTER_COLS + (NUM_LEFT_COLS * y) + y;
      xyu = xyl + (left ? NUM_LEFT_COLS : NUM_RIGHT_COLS) - 1;
      if (serpentine && y & 0x01) { // odd rows need reversed
        xyl = (NUM_LEDS - 1) - xyl;
        xyu = (NUM_LEDS - 1) - xyu;
        xyl ^= xyu; // swap
        xyu ^= xyl;
        xyl ^= xyu;
      }
      //Serial.printf("%u) %u (xy PRE-ADJUST) %u (xy) %u (lower) %u (upper)\n", y, xy, map(xy, xycl, xycu, xyl, xyu), xyl, xyu);
      xy = map(xy, xycl, xycu, xyl, xyu); // map to submatrix range
      break;
    }
  }
}
// random fire-like movement, hot (0...255) increase for brightness, cooling (0...255) increase for smaller fire
void fire(uint8_t hot = 120, uint8_t cooling = 120) {
  static uint16_t spark[COLS]; // base heat
  CRGB stack[COLS][ROWS]; // stacks that are cooler
  uint16_t hotMax = hot * ROWS;
  uint16_t hot2x = hot * 2;
  uint16_t hotHalf = hot >> 1;
  for (int i = 0; i < COLS; i++) {
    if (spark[i] < hot) spark[i] = random16(hot2x, hotMax); // re-heat spark
    spark[i] = qsub8(spark[i], random8(0, cooling)); // cool the spark
  }
  for (int i = 0; i < COLS; i++) { // stack it up... this works on the idea that pixels are "cooler" as they get further from the spark at the bottom
    unsigned int heat = constrain(spark[i], hot / 2, hotMax);
    for (int j = ROWS - 1; j >= 0; j--) {
      byte index = constrain(heat, 0, hot); // calculate the color on the palette from how hot this pixel is
      stack[i][j] = ColorFromPalette(HeatColors_p, index);
      unsigned int drop = random8(0, hot); // calculate the drop since the next higher pixel will be cooler
      if (drop > heat) heat = 0; // avoid wrap-arounds from going negative
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
// constrains a beatsin16 to a matrix row region complete with an optional offset and/or flip option that allows beats to be reversed
Pulse pulsify(const uint8_t bpm, const uint16_t y, const uint16_t width = NUM_LEDS, const int16_t offset = 0, const bool flip = false, const bool serpentine = MATRIX_SERPENTINE) {
  Pulse pulse;
  pulse.lower = (COLS * y) + offset;
  pulse.upper = pulse.lower + width - 1;
  pulse.beat = pulse.xy = beatsin16(bpm, pulse.lower, pulse.upper);
  if (serpentine && y & 0x01) {
    pulse.xy = (NUM_LEDS - 1) - pulse.xy; // need to reverse position for odd rows
    pulse.lower = (NUM_LEDS - 1) - pulse.lower; // reverse lower bounds
    pulse.upper = (NUM_LEDS - 1) - pulse.upper; // reverse upper bounds
    pulse.lower ^= pulse.upper; // lower/upper swap
    pulse.upper ^= pulse.lower;
    pulse.lower ^= pulse.upper;
  }
  if (flip) { // need to reverse horizontal direction
    pulse.xy = (NUM_LEDS - 1) - pulse.xy + (serpentine && y & 0x01 ? -offset : offset);
    pulse.beat = (NUM_LEDS - 1) - pulse.beat + (serpentine && y & 0x01 ? -offset : offset);
    pulse.lower = (NUM_LEDS - 1) - pulse.lower + (serpentine && y & 0x01 ? -offset : offset);
    pulse.upper = (NUM_LEDS - 1) - pulse.upper + (serpentine && y & 0x01 ? -offset : offset);
    pulse.lower ^= pulse.upper; // lower/upper swap
    pulse.upper ^= pulse.lower;
    pulse.lower ^= pulse.upper;
  }
  return pulse;
}
// same as sweep except travels in a single direction based upon delta reversal (i.e. right-to-left instead of left-to-right)
Pulse turn(Pulse& turned, const CRGBPalette16& palette, uint8_t& colorIndex, const uint8_t bpm, uint8_t& fadeRate, const uint16_t y, const uint16_t width = NUM_LEDS, const bool leftToRight = true, const int16_t offset = 0, const bool serpentine = MATRIX_SERPENTINE) {
  // left turn signal or right turn signal w/offset for center column/light
  const CRGB color = ColorFromPalette(palette, colorIndex, 255, NOBLEND);
  Pulse turn = !leftToRight ? pulsify(bpm, y, width) : pulsify(bpm, y, width, width + 1, true);
  leds[turn.xy] = color;
  //leds[turn.xy] %= 255 - (turn.frac * 16); // (dark: 0..255 :light) 25% = 64/256
  if (y == 0 && turn.beat != turned.beat) { // only need delta on first row since all rows are mirrored
    if (turn.beat > turned.beat) turned.delta |= TURN_RISING;
    else if (turn.beat < turned.beat) turned.delta &= ~TURN_RISING;
  }
  turn.delta = turned.delta; // sync every row w/pre-calculated flags from 1st row
  //Serial.printf("%u) %u (turned.beat) %u (turn.beat) %u (turn.lower) %u (turn.upper)\n", y, turned.beat, turn.beat, turn.lower, turn.upper);
  if ((!leftToRight && turn.delta & TURN_RISING) /* <- left turn */ || (leftToRight && !(turn.delta & TURN_RISING)) /* <- right turn */) { // backwards direction
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
void turnSignal(const CRGBPalette16& palette, const bool left, const bool right) {
  if (!left && !right) return;
  for (uint16_t y = 0; y < ROWS; y++) {
    if (left) turn(turn_left, palette, turn_left_color_index, turn_bpm, turn_fade, y, NUM_LEFT_COLS, false); // left turn signal (all rows are mirrored)
    if (right) turn(turn_right, palette, turn_right_color_index, turn_bpm, turn_fade, y, NUM_RIGHT_COLS, NUM_RIGHT_COLS + NUM_CENTER_COLS); // right turn signal w/offset for center column/light
  }
}
// flickering white dots in random locations
void glitter(const bool left, const bool right, const uint8_t confettiBPM = 0, const fract8 chanceOfGlitter = 80, const bool serpentine = MATRIX_SERPENTINE) {
  if (!left && !right) return;
  if (random8() < chanceOfGlitter) {
    uint16_t xy = random16(NUM_LEDS);
    mapToSide(xy, left, right, true, serpentine); // map to either the left or right side when doesnt span the entire display
    if (confettiBPM > 0) leds[xy] += CHSV(beatsin8(confettiBPM) + random8(64), 200, 255);
    else leds[xy] += CRGB::White;
  }
}
// a colored dot sweeping back and forth, with fading trails
void sinelon(uint8_t hue) {
  leds[beatsin16(13, 0, NUM_LEDS - 1)] += CHSV(hue, 255, 192);
}
// 4 (1/2 display) or 8 (full display) colored pixels weaving in and out of sync with each other
void juggle(const CRGBPalette16& palette, const bool left, const bool right, uint8_t speedOffset = 0, const bool serpentine = MATRIX_SERPENTINE) {
  if (!left && !right) return;
  const uint8_t bcnt = left && right ? 8 : 4;
  if (speedOffset == 0) speedOffset = left && right ? bcnt - 1 : bcnt * 4;
  uint16_t xy = 0;
  for (uint8_t i = 0; i < bcnt; i++) {
    xy = beatsin16(i + speedOffset, 0, NUM_LEDS - 1);
    mapToSide(xy, left, right, true, serpentine); // map to either the left or right side when doesnt span the entire display
    leds[xy] = palette[beatsin8(i + speedOffset, 0, 15)]; //addmod8(i, 1, 16);
    //leds[xy] |= CHSV(hue, 200, 255);
    //hue += 32;
  }
}
// 8-bit inoise8 patterns: cycleColor true to cycle colors vs using them all at once, speedo to increase/decrease animation speed (1..60), scale invert to increase/decrease size of animated regions
void noise8(const CRGBPalette16& palette, const bool left, const bool right, const bool cycleColor = false, const uint16_t speedo = 8, const uint16_t scale = 100, const bool serpentine = MATRIX_SERPENTINE) {
  if (!left && !right) return;
  static uint8_t noise_hue = 0;
  static uint16_t noise_x = random16(), noise_y = random16(), noise_z = random16(); // z-axis = time
  uint8_t dataSmoothing = speedo < 50 ? 200 - (speedo * 4) : 0; // smooth out some 8-bit artifacts that become visible from frame-to-frame when running at a low speed
  uint8_t xstart = !left && right ? NUM_LEFT_COLS + NUM_CENTER_COLS : 0, width = left && !right ? NUM_LEFT_COLS : COLS, maxDimension = width > ROWS ? width : ROWS, data = 0, ci = 0, luz = 0;
  uint16_t ioffset = 0, joffset = 0;
  for (uint8_t i = 0; i < maxDimension; i++) {
    ioffset = scale * i;
    for (uint8_t j = 0; j < maxDimension; j++) {
      joffset = scale * j;
      data = inoise8(noise_x + ioffset, noise_y + joffset, noise_z);
      // range of inoise8 function is roughly 16-238 which qsub8 is expanding to about 0..255
      data = qsub8(data, 16);
      data = qadd8(data, scale8(data, 39));
      if (dataSmoothing) data = scale8(noise_matrix[i][j], dataSmoothing) + scale8(data, 256 - dataSmoothing);
      noise_matrix[i][j] = data;
    }
  }
  noise_z += speedo;
  noise_x += speedo / 8; // slowly drift for visual variation
  noise_y -= speedo / 16; // slowly drift for visual variation
  for (uint8_t y = 0; y < ROWS; y++) { // map LED colors using the calculated noise
    for (uint8_t x = xstart; x < width; x++) {
      ci = noise_matrix[y][x]; // color index
      luz = noise_matrix[x][y]; // brightness
      if (cycleColor) ci += noise_hue; // if this palette is a loop, add a slowly-changing base value
      luz = luz > 127 ? 255 : dim8_raw(luz * 2); // brighten as the color palette itself often contains the desired/dynamic light/dark range
      leds[XY(x, y, width, serpentine)] = ColorFromPalette(palette, ci, luz);
    }
  }
  if (cycleColor) noise_hue++;
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
    //memcpy8(buffer, leds, sizeof(leds)); // copy values from buffer to LEDs
    //memset8(leds, 0, NUM_LEDS * sizeof(CRGB)); // clear the LED pixel buffer
    if (flags & BRAKE_ON) {
      // brake animation on
    } else if (!(flags & LEFT_ON && flags & RIGHT_ON)) { // ensure that the warning lights are not on (i.e. simultaneous left/right turn signals)
      // blackout the pixels where the running lamp animation will be displayed
      if (!(flags & LEFT_ON) && !(flags & RIGHT_ON)) fadeToBlackBy(leds, NUM_LEDS, 255); // 8 bit, 1 = slow, 255 = fast
      else if (flags & LEFT_ON) blackout(NUM_LEFT_COLS + NUM_CENTER_COLS, COLS, 0, ROWS, 255); // blackout right side of the display only
      else blackout(0, NUM_LEFT_COLS, 0, ROWS, 255); // blackout left side of the display only
      // run selected running lamp animation(s)
      if (run_anim_flags & ANIM_NOISE) noise8(RunPalette_p, !(flags & LEFT_ON), !(flags & RIGHT_ON), run_color_cycle, run_speed, run_scale);
      if (run_anim_flags & ANIM_JUGGLE) juggle(RunPalette_p, !(flags & LEFT_ON), !(flags & RIGHT_ON));
      if (run_anim_flags & ANIM_CONFETTI) glitter(!(flags & LEFT_ON), !(flags & RIGHT_ON), true);
      if (run_anim_flags & ANIM_GLITTER) glitter(!(flags & LEFT_ON), !(flags & RIGHT_ON));
      /*EVERY_N_MILLISECONDS(FPS_FIRE_MILLIS) {
        fire();
        FastLED.show();
      }*/
    }
    //if (DEBUG) Serial.printf("Left turn signal is %s, Right turn signal is %s\n", flags & LEFT_ON ? "ON" : flags & LEFT_OFF ? "OFF" : "N/A", flags & RIGHT_ON ? "ON" : flags & RIGHT_OFF ? "OFF" : "N/A");
    if (flags & LEFT_ON || flags & RIGHT_ON) turnSignal(TurnPalette_p, flags & LEFT_ON, flags & RIGHT_ON);
    blackout(COL_CENTER_INDEX, COL_CENTER_INDEX + 1, 0, ROWS, 0); // center channel should not be lit
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
  //turnLeftOn();
  //turnRightOn();
}
