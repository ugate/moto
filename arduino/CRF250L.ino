
/*
 CRF250L Edge 2 mod for integrated tail light
 WS2812B mini LED pixel PCB board (24pcs)
 LM2596 Mini 360 DC/DC Buck/Step-down converter (5pcs)
 Ardiono Nano or ESP8266 if web interface is desired (1pc)
 */

const byte debug = 1;
const unsigned long turnMillis = 1500;      // rate which turn animations will need to ignore sudden drops due to external relays

const byte brakePin = 0;                    // the pin the brake light is attached to
const byte leftPin = 4;                     // the pin the left turn signal is attached to
const byte rightPin = 5;                    // the pin the right turn signal is attached to

volatile byte brakeIntOnCnt = 0;            // brake light on interrupt count
volatile byte brakeIntOffCnt = 0;           // brake light off interrupt count
unsigned long leftOffMillis = 0;            // to avoid false-positives from any extranal relays
unsigned long leftOffMillisPrev = 0;        // to avoid false-positives from any extranal relays
volatile byte leftIntOnCnt = 0;             // left turn signal on interrupt count
volatile byte leftIntOffCnt = 0;            // left turn signal off interrupt count
unsigned long rightOffMillis = 0;           // current time 
unsigned long rightOffMillisPrev = 0;       // to avoid false-positives from any extranal relays
volatile byte rightIntOnCnt = 0;            // right turn signal on interrupt count
volatile byte rightIntOffCnt = 0;           // right turn signal off interrupt count

// the setup routine runs once when you press reset:
void setup() {
  if (debug) {
    Serial.begin(115200);
    Serial.println("Starting...");
  }
  pinMode(brakePin, INPUT);
  pinMode(leftPin, INPUT);
  pinMode(rightPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(brakePin), brakeIntOn, HIGH);
  attachInterrupt(digitalPinToInterrupt(brakePin), brakeIntOff, FALLING);
  attachInterrupt(digitalPinToInterrupt(leftPin), leftIntOn, HIGH);
  attachInterrupt(digitalPinToInterrupt(leftPin), leftIntOff, FALLING);
  attachInterrupt(digitalPinToInterrupt(rightPin), rightIntOn, HIGH);
  attachInterrupt(digitalPinToInterrupt(rightPin), rightIntOff, FALLING);
}

// the loop routine runs over and over again forever:
void loop() {
  if (brakeIntOnCnt > 0) {
    brakeIntOnCnt = 0; // reset
    if (debug) Serial.println("Turning brake lights on");
    // brake animation on
  } else if (brakeIntOffCnt > 0) {
    brakeIntOffCnt = 0; // reset
    if (debug) Serial.println("Turning brake lights off");
    // brake animation off
  }
  if (leftIntOffCnt > 0) {
    leftIntOffCnt = 0; // reset
    leftOffMillis = millis();
    if (leftOffMillis - leftOffMillisPrev > turnMillis) {
      if (debug) {
        Serial.print("Turning left turn signal on. Elapsed Time: ");
        Serial.println(leftOffMillis - leftOffMillisPrev);
      }
      leftOffMillisPrev = leftOffMillis;
      // left turn signal animation off
    }
  }
  if (rightIntOffCnt > 0) {
    rightIntOffCnt = 0; // reset
    rightOffMillis = millis();
    if (rightOffMillis - rightOffMillisPrev > turnMillis) {
      if (debug) {
        Serial.print("Turning right turn signal on. Elapsed Time: ");
        Serial.println(rightOffMillis - rightOffMillisPrev);
      }
      rightOffMillisPrev = rightOffMillis;
      // right turn signal animation off
    }
  }
  if (leftIntOnCnt > 0) {
    leftIntOnCnt = 0; // reset
    if (debug) Serial.println("Turning left turn signal on");
    // left turn signal animation on
  }
  if (rightIntOnCnt > 0) {
    rightIntOnCnt = 0; // reset
    if (debug) Serial.println("Turning right turn signal on");
    // right turn signal animation on
  }
}

void brakeIntOn() {
  brakeIntOffCnt = 0;
  brakeIntOnCnt++;
}

void brakeIntOff() {
  brakeIntOnCnt = 0;
  brakeIntOffCnt++;
}

void leftIntOn() {
  leftIntOffCnt = 0;
  leftIntOnCnt++;
}

void leftIntOff() {
  leftIntOnCnt = 0;
  leftIntOffCnt++;
}

void rightIntOn() {
  rightIntOffCnt = 0;
  rightIntOnCnt++;
}

void rightIntOff() {
  rightIntOnCnt = 0;
  rightIntOffCnt++;
}
