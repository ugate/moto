// ----------------- FASTLED ------------------------ 

// resets the general data values (saves memory by sharing
void resetData() {
  const bool isNoise = (led_flags & BRAKE_ON && brake_anim_flags & ANIM_NOISE) || (!(led_flags & BRAKE_ON) && run_anim_flags & ANIM_NOISE);
  data_millis = 0;
  data16_x = isNoise ? random16() : 0;
  data16_y = isNoise ? random16() : 0;
  data16_z = isNoise ? random16() : 0;
  data8_x = 0;
}
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
    xy = (y * numCols) + reverseX;//Serial.printf("(%u:y * %u:numCols) + (%u:numCols - 1) - %u:x = %u:xy\n", y, numCols, numCols, x, xy);
  }
  return xy;
}
// blends one uint8_t to another by a specified dimming amount
void nblendU8TowardU8(uint8_t& cur, const uint8_t& target, uint8_t amount) {
  if (cur == target) return;
  if (cur < target) cur += scale8_video(target - cur, amount);
  else cur -= scale8_video(cur - target, amount);
}
// blends the current CRGB towards a target CRGB by a given dimming amount
CRGB fadeTowardColor(CRGB& cur, const CRGB& target, uint8_t amount) {
  nblendU8TowardU8(cur.red, target.red, amount);
  nblendU8TowardU8(cur.green, target.green, amount);
  nblendU8TowardU8(cur.blue, target.blue, amount);
  return cur;
}
// fills a matrix pixel region (fades toward rgb from thecurrent color when fade > 0)
void fill_matrix(const CRGB rgb, const uint16_t xy1, const uint16_t xy2, const uint8_t fade = 0) {//Serial.printf("FILL MATRIX: rgb(%u, %u, %u) %u (pixel start) %u (pixel end) %u (fade rate)\n", rgb.r, rgb.g, rgb.b, xy1, xy2, fade);
  for (uint16_t xy = xy1; xy <= xy2; xy++) {
    if (fade > 0) fadeTowardColor(leds[xy], rgb, fade); //leds[xy].nscale8(fade);
    else leds[xy] = rgb;
  }
}
// fadeToBlackBy, but with a matrix pixel range and option for immediate (i.e. fade == 0)
void fill_matrix(const CRGB rgb, const uint16_t xstart, const uint16_t width, const uint16_t ystart, const uint16_t height, const uint8_t fade = 0, const bool serpentine = MATRIX_SERPENTINE) {
  uint16_t xy = 0;//Serial.printf("FILL MATRIX: rgb(%u, %u, %u) %u (x start) %u (width) %u (y start) %u (height) %u (fade rate)\n", rgb.r, rgb.g, rgb.b, xstart, width, ystart, height, fade);
  for (uint16_t y = ystart; y < height; y++) {
    for (uint16_t x = xstart; x < width; x++) {
      xy = XY(x, y, width, serpentine);
      fill_matrix(rgb, xy, xy, fade);
    }
  }
}
// fills a display with a color
void fill_display(const CRGB rgb, const bool left, const bool right, const uint8_t fade = 0, const bool serpentine = MATRIX_SERPENTINE) {
  if (!left && !right) return;
  if (left && right && rgb.getLuma() == 0) fadeToBlackBy(leds, NUM_LEDS, fade); // fade: 8 bit, 1 = slow, 255 = fast
  else if (left && right && fade == 255) fill_solid(leds, NUM_LEDS, rgb);
  else if (left && right) fill_matrix(rgb, 0, COLS, 0, ROWS, fade);
  else if (left) fill_matrix(rgb, 0, NUM_LEFT_COLS, 0, ROWS, fade);
  else fill_matrix(rgb, NUM_LEFT_COLS + NUM_CENTER_COLS, COLS, 0, ROWS, fade);
}
// similar to the built-in map function except it is confined to a submatrix region that spans multiple rows on the left/right side of the display
void mapToSide(uint16_t& xy, const bool left, const bool right, const bool clearRegion = false, const bool serpentine = MATRIX_SERPENTINE) {
  if (left && right) return; // nothing to map, pixels fill the entire matrix
  for (uint16_t y = 0, xycl = 0, xycu = 0, xyl = 0, xyu = 0; y < ROWS; y++) {
    xycl = COLS * y;
    xycu = xycl + COLS - 1;
    if (clearRegion) fill_matrix(CRGB::Black, xycl, xycu);
    if (xy >= xycl && xy <= xycu) { // xy falls within row
      xyl = left ? COLS * y : NUM_LEFT_COLS * (y + 1) + NUM_CENTER_COLS + (NUM_LEFT_COLS * y) + y;
      xyu = xyl + (left ? NUM_LEFT_COLS : NUM_RIGHT_COLS) - 1;
      if (serpentine && y & 0x01) { // odd rows need reversed
        xyl = (NUM_LEDS - 1) - xyl;
        xyu = (NUM_LEDS - 1) - xyu;
        xyl ^= xyu; // swap
        xyu ^= xyl;
        xyl ^= xyu;
      }//Serial.printf("%u) %u (xy PRE-ADJUST) %u (xy) %u (lower) %u (upper)\n", y, xy, map(xy, xycl, xycu, xyl, xyu), xyl, xyu);
      xy = map(xy, xycl, xycu, xyl, xyu); // map to submatrix range
      break;
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
    if (!(turn.delta & TURN_STAY_LIT_BACKWARDS)) fill_matrix(CRGB::Black, turn.lower, turn.upper, 0);
    if ((!leftToRight && turn.beat == turn.upper) /* <- left turn */ || (leftToRight && turn.beat == turn.lower) /* <- right turn */) colorIndex++;
  } else if (turn.delta & TURN_CYLON) {
    fill_matrix(CRGB::Black, turn.lower, turn.upper, 0);
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
// random fire-like movement, hot (0...255) increase for brightness, cooling (0...255) increase for smaller fire
void fire(const CRGBPalette16& palette, const bool left, const bool right, uint8_t hot = 120, uint8_t cooling = 120) {
  if (!left && !right) return;
  fill_display(CRGB::Black, left, right); //Serial.printf("BLACKOUT: %d (left) %d (right)\n", left, right);
  uint8_t xstart = !left && right ? NUM_LEFT_COLS + NUM_CENTER_COLS : 0, width = left && !right ? NUM_LEFT_COLS : COLS;
  /*static*/ uint16_t spark[COLS]; // base heat
  CRGB stack[width][ROWS]; // stacks that are cooler
  uint16_t hotMax = hot * ROWS;
  uint16_t hot2x = hot * 2;
  uint16_t hotHalf = hot >> 1;
  for (int x = xstart; x < width; x++) {
    if (spark[x] < hot) spark[x] = random16(hot2x, hotMax); // re-heat spark
    spark[x] = qsub8(spark[x], random8(0, cooling)); // cool the spark
  }
  for (int x = xstart; x < width; x++) { // stack it up... this works on the idea that pixels are "cooler" as they get further from the spark at the bottom
    unsigned int heat = constrain(spark[x], hot / 2, hotMax);
    for (int y = ROWS - 1; y >= 0; y--) {
      byte index = constrain(heat, 0, hot); // calculate the color on the palette from how hot this pixel is
      stack[x][y] = ColorFromPalette(palette, index);
      unsigned int drop = random8(0, hot); // calculate the drop since the next higher pixel will be cooler
      if (drop > heat) heat = 0; // avoid wrap-arounds from going negative
      else heat -= drop;
      heat = constrain(heat, 0, hotMax);
    }
  }
  // 4. map stacks to led array
  for (int x = xstart; x < width; x++) {
    for (int y = 0; y < ROWS; y++) {
      leds[(y * width) + x] = stack[x][y];
    }
  }
}
// random rain droplets, chanceOfRain in percentage (1%-100%) increases/decreases the number of simultaneous droplets falling at a given moment
void rain(const CRGBPalette16& palette, const bool left, const bool right, const uint8_t wait = 100, const uint8_t chanceOfRain = 30, const bool serpentine = MATRIX_SERPENTINE) {
  if ((!left && !right) || chanceOfRain == 0) return;
  const uint8_t xstart = !left && right ? NUM_LEFT_COLS + NUM_CENTER_COLS : 0, width = left && right ? COLS : left ? NUM_LEFT_COLS : NUM_RIGHT_COLS;
  const uint8_t maxd = chanceOfRain >= 100 ? width : round((width / 100.00) * chanceOfRain), change = millis() - data_millis;
  uint16_t x = xstart, y = 0, xy = 0;
  uint8_t cntd = 0;
  const bool inc = change > wait;
  if (inc) data_millis = millis();
  CRGB rgb;
  for (; x < width; ++x) { // for each column, move existing droplets down until they disappear
    rgb = CRGB::Black;
    y = 0;
    for (; y < ROWS; ++y) {
      xy = XY(x, y, width, serpentine);
      if (leds[xy]) cntd++;
      if (rgb) {
        leds[xy] = rgb; //leds[xy] = rgb;
        rgb = CRGB::Black;//Serial.printf("MOVED DROPLET: %u (x) %u (y) %u (max droplets) %u (droplet count)\n", x, y, maxd, cntd);
        if (++cntd >= maxd) return; // we have enough droplets
        break;
      } else if (leds[xy]) {
        if (inc) {//Serial.printf("MOVING DROPLET: %u (x) %u (y) %u (max droplets) %u (droplet count)\n", x, y, maxd, cntd);
          rgb = leds[xy]; // proceed to light next row
          leds[xy] = CRGB::Black;
        } //else leds[xy].fadeToBlackBy(change);
      }
    }
  }
  if (!inc || cntd >= maxd) return; // no droplet movement or we have enough droplets
  for (uint16_t xi = xstart, xyf = 0; xi < width; ++xi) { // form new random droplets that do not already occupy the same column
    if (cntd >= maxd) break; // we have enough droplets
    x = random8(xstart, width - 1);
    y = 0;
    for (; y < ROWS; ++y) {
      xy = XY(x, y, width, serpentine);
      if (y == 0) xyf = xy;
      if (leds[xy]) { // occupied
        x = width;
        break;
      }
    }//if (x < width) Serial.printf("NEW DROPLET: %u (x) %u (width) %u (max droplets) %u (droplet count)\n", x, width, maxd, cntd + 1);
    if (x < width && ++cntd) {
      rgb = ColorFromPalette(palette, brake_color_index, 255);
      leds[xyf] = rgb;
      brake_color_index++;
    }
  }//Serial.printf("---> END DROPLET: %u (max droplets) %u (droplet count)\n", maxd, cntd);
}
// 4 (1/2 display) or 8 (full display) colored pixels weaving in and out of sync with each other
void juggle(const CRGBPalette16& palette, const bool left, const bool right, uint8_t speedOffset = 0, const bool serpentine = MATRIX_SERPENTINE) {
  if (!left && !right) return;
  fill_display(CRGB::Black, left, right); //Serial.printf("BLACKOUT: %d (left) %d (right)\n", left, right);
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
// pulsates through colors in a color palette: traceColor will run the perimeter of the display (or CRGB::Black to ommit tracing),
// fade value changes how drastic color changes appear (0 = immediate, 1 = faint, 255 = drastic), wait is the delay in MS between color changes
void pulse(const CRGBPalette16& palette, const bool left, const bool right, const CRGB traceColor = CRGB::White, const uint8_t fade = 10, const uint8_t wait = 50, const bool serpentine = MATRIX_SERPENTINE) {
  if (!left && !right) return;
  const uint8_t change = millis() - data_millis;
  if (change > wait) {
    data_millis = millis();
    data8_x++;
  }
  CRGB rgb = palette[scale8(data8_x, 15)];
  fill_display(rgb, left, right, fade);
  if (traceColor.getLuma() > 0) {
    const uint8_t width = left && right ? COLS : left ? NUM_LEFT_COLS : NUM_RIGHT_COLS;
    const uint16_t xmin = left ? 0 : NUM_LEFT_COLS + NUM_CENTER_COLS, xmax = right ? COLS - 1 : NUM_LEFT_COLS - 1;
    if (data16_x < xmin) data16_x = xmin;
    if (data16_y < 0) data16_y = 0;
    data16_z = data16_z == RIGHT && data16_x + 1 > xmax ? DOWN : data16_z == DOWN && data16_y + 1 > ROWS - 1 ? LEFT : data16_z == LEFT && data16_x - 1 < xmin ? UP : data16_z == UP && data16_y - 1 < 0 ? RIGHT : data16_z;
    if (change > wait) data16_y += data16_z == DOWN ? 1 : data16_z == UP ? -1 : 0;
    if (change > wait) data16_x += data16_z == RIGHT ? 1 : data16_z == LEFT ? -1 : 0;
    uint16_t xy = XY(data16_x, data16_y, COLS, serpentine);
    leds[xy] = traceColor; //Serial.printf("PULSE %u (x) %u (y) %u (direction) %u (xmin) %u (xmax) %u (xy)\n", data16_x, data16_y, data16_z, xmin, xmax, xy);
  }
}
// 8-bit inoise8 patterns: speedo to increase/decrease animation speed (1..60), scale invert to increase/decrease size of animated regions, cycleColor true to cycle colors vs using them all at once
void noise8(const CRGBPalette16& palette, const bool left, const bool right, const uint16_t speedo = 8, const uint16_t scale = 100, const bool cycleColor = false, const bool serpentine = MATRIX_SERPENTINE) {
  if (!left && !right) return;
  fill_display(CRGB::Black, left, right); //Serial.printf("BLACKOUT: %d (left) %d (right)\n", left, right); 
  uint8_t smooth = speedo < BPM ? 200 - (speedo * 4) : 0; // smooth out some 8-bit artifacts that become visible from frame-to-frame when running at a low speed
  uint8_t xstart = !left && right ? NUM_LEFT_COLS + NUM_CENTER_COLS : 0, width = left && !right ? NUM_LEFT_COLS : COLS, maxDimension = width > ROWS ? width : ROWS, data = 0, ci = 0, luz = 0;
  uint16_t ioffset = 0, joffset = 0;
  for (uint8_t i = 0; i < maxDimension; i++) {
    ioffset = scale * i;
    for (uint8_t j = 0; j < maxDimension; j++) {
      joffset = scale * j;
      data = inoise8(data16_x + ioffset, data16_y + joffset, data16_z);
      // range of inoise8 function is roughly 16-238 which qsub8 is expanding to about 0..255
      data = qsub8(data, 16);
      data = qadd8(data, scale8(data, 39));
      if (smooth) data = scale8(data8_matrix[i][j], smooth) + scale8(data, 256 - smooth);
      data8_matrix[i][j] = data;
    }
  }
  data16_z += speedo; // z-axis = time
  data16_x += speedo / 8; // x-axis slowly drift for visual variation
  data16_y -= speedo / 16; // y-axis slowly drift for visual variation
  for (uint8_t y = 0; y < ROWS; y++) { // map LED colors using the calculated noise
    for (uint8_t x = xstart; x < width; x++) {
      ci = data8_matrix[y][x]; // color index
      luz = data8_matrix[x][y]; // brightness
      if (cycleColor) ci += data8_x; // if this palette is a loop, add a slowly-changing base value
      luz = luz > 127 ? 255 : dim8_raw(luz * 2); // brighten as the color palette itself often contains the desired/dynamic light/dark range
      leds[XY(x, y, width, serpentine)] = ColorFromPalette(palette, ci, luz);
    }
  }
  if (cycleColor) data8_x++;
}
// debounce flag check for off condition (prevents in sudden fluctuations when turning flags off)
void flagged(byte onFlag, byte offFlag, unsigned long* msp) {
  if (led_flags & offFlag) { // off flagged
    if (millis() - *msp > DETECT_MILLIS) {//Serial.printf("%s off. Elapsed Time: %d\n", offFlag == LEFT_OFF ? "Left turn signal" : RIGHT_OFF ? "Right turn signal" : "Brake lights", millis() - *msp);
      *msp = millis();
      led_flags &= ~onFlag; // remove on flag
    }
  }
}
bool brakeOn() {
  led_flags &= ~BRAKE_OFF; // remove
  led_flags |= BRAKE_ON; // add
  //Serial.printf("Brakes on... BRAKE_ON: %d, BRAKE_OFF: %d\n", led_flags & BRAKE_ON, led_flags & BRAKE_OFF);
  return led_flags & BRAKE_ON && !(led_flags & BRAKE_OFF);
}
bool brakeOff() {
  led_flags |= BRAKE_OFF; // add
  //Serial.printf("Brakes off... BRAKE_ON: %d, BRAKE_OFF: %d\n", led_flags & BRAKE_ON, led_flags & BRAKE_OFF);
  return led_flags & BRAKE_OFF;
}
bool turnLeftOn() {
  led_flags &= ~LEFT_ON; // remove
  led_flags |= LEFT_ON; // add
  //Serial.printf("Left turn signal on... LEFT_ON: %d, LEFT_OFF: %d\n", led_flags & LEFT_ON, led_flags & LEFT_OFF);
  return led_flags & LEFT_ON && !(led_flags & LEFT_OFF);
}
bool turnLeftOff() {
  led_flags |= LEFT_OFF; // add
  //Serial.printf("Left turn signal off... LEFT_OFF: %d\n", led_flags & LEFT_OFF);
  return led_flags & LEFT_OFF;
}
bool turnRightOn() {
  led_flags &= ~RIGHT_OFF; // remove
  led_flags |= RIGHT_ON; // add
  //Serial.printf("Right turn signal on... RIGHT_ON: %d, RIGHT_OFF: %d\n", led_flags & RIGHT_ON, led_flags & RIGHT_OFF);
  return led_flags & RIGHT_ON && !(led_flags & RIGHT_OFF);
}
bool turnRightOff() {
  led_flags |= RIGHT_OFF; // add
  //Serial.printf("Right turn signal off... RIGHT_OFF: %d\n", led_flags & RIGHT_OFF);
  return led_flags & RIGHT_OFF;
}

// should be called in the main loop
void ledLoop() {
  flagged(BRAKE_ON, BRAKE_OFF, &brakeOffMillisPrev); // check if brake is on usng time threshold
  flagged(LEFT_ON, LEFT_OFF, &leftOffMillisPrev); // check if left turn signal is on usng time threshold
  flagged(RIGHT_ON, LEFT_OFF, &rightOffMillisPrev); // check if right turn signal is on usng time threshold
  EVERY_N_MILLISECONDS(FPS_ANIM_MILLIS) {
    //memcpy8(buffer, leds, sizeof(leds)); // copy values from buffer to LEDs
    //memset8(leds, 0, NUM_LEDS * sizeof(CRGB)); // clear the LED pixel buffer
    if (!(led_flags & LEFT_ON && led_flags & RIGHT_ON)) { // ensure that the warning lights are not on (i.e. simultaneous left/right turn signals)
      // run selected brake or running lamp animation(s)
      const TProgmemPalette16 *palette = led_flags & BRAKE_ON ? &BrakePalette_p : &RunPalette_p;
      if (led_flags & BRAKE_ON && brake_anim_flags & ANIM_NOISE) noise8(*palette, !(led_flags & LEFT_ON), !(led_flags & RIGHT_ON), brake_speed, brake_scale);
      else if (!(led_flags & BRAKE_ON) && run_anim_flags & ANIM_NOISE) noise8(*palette, !(led_flags & LEFT_ON), !(led_flags & RIGHT_ON), run_speed, run_scale);
      else if ((led_flags & BRAKE_ON && brake_anim_flags & ANIM_PULSE) || (!(led_flags & BRAKE_ON) && run_anim_flags & ANIM_PULSE))
        pulse(*palette, !(led_flags & LEFT_ON), !(led_flags & RIGHT_ON), led_flags & BRAKE_ON && (led_flags & LEFT_ON || led_flags & RIGHT_ON) ? CRGB::Black : CRGB::White, 20); // turn signal + brake = no tracing
      else if ((led_flags & BRAKE_ON && brake_anim_flags & ANIM_FIRE) || (!(led_flags & BRAKE_ON) && run_anim_flags & ANIM_FIRE)) fire(*palette, !(led_flags & LEFT_ON), !(led_flags & RIGHT_ON));
      else if ((led_flags & BRAKE_ON && brake_anim_flags & ANIM_RAIN) || (!(led_flags & BRAKE_ON) && run_anim_flags & ANIM_RAIN)) rain(*palette, !(led_flags & LEFT_ON), !(led_flags & RIGHT_ON));
      else if ((led_flags & BRAKE_ON && brake_anim_flags & ANIM_JUGGLE) || (!(led_flags & BRAKE_ON) && run_anim_flags & ANIM_JUGGLE)) juggle(*palette, !(led_flags & LEFT_ON), !(led_flags & RIGHT_ON));
      if ((led_flags & BRAKE_ON && brake_anim_flags & ANIM_CONFETTI) || (!(led_flags & BRAKE_ON) && run_anim_flags & ANIM_CONFETTI)) glitter(!(led_flags & LEFT_ON), !(led_flags & RIGHT_ON), true);
      if ((led_flags & BRAKE_ON && brake_anim_flags & ANIM_GLITTER) || (!(led_flags & BRAKE_ON) && run_anim_flags & ANIM_GLITTER)) glitter(!(led_flags & LEFT_ON), !(led_flags & RIGHT_ON));
    }
    //Serial.printf("Left turn signal is %s, Right turn signal is %s\n", led_flags & LEFT_ON ? "ON" : led_flags & LEFT_OFF ? "OFF" : "N/A", led_flags & RIGHT_ON ? "ON" : led_flags & RIGHT_OFF ? "OFF" : "N/A");
    if (led_flags & LEFT_ON || led_flags & RIGHT_ON) turnSignal(TurnPalette_p, led_flags & LEFT_ON, led_flags & RIGHT_ON);
    //fill_matrix(CRGB::Black, COL_CENTER_INDEX, COL_CENTER_INDEX + 1, 0, ROWS, 0); // center channel should not be lit
    FastLED.show();
  }
  //Serial.println(LEDS.getFPS());
}

// should be called in the main setup
void ledSetup() {
  led_flags |= RUN_ON; // add
  brakeOffMillisPrev = leftOffMillisPrev = rightOffMillisPrev = millis();
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
  //FastLED.clear(); FastLED.show();
  statusIndicator(SETUP_STAT_LED_COMPLETE);
  resetData();
}
