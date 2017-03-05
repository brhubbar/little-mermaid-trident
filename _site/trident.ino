#include <Arduino.h>

#include <FastLED.h>

#define DATAPIN    10
#define CLOCKPIN   12

#define ATTACK_BUTTON_PIN 5
#define MAGIC_BUTTON_PIN 4
#define MODE_PIN 6
#define VELO_PIN_1 A5  // holding sensor
#define VELO_PIN_2 A4  // secondary pressure
#define VELO_PIN_3 A3  // tertiary pressure

#define COLOR_ORDER BGR
#define CHIPSET     DOTSTAR
#define NUM_SHAFT_LEDS 60
#define NUM_TINE1_LEDS 10
#define NUM_TINE2_LEDS 10
#define NUM_TINE3_LEDS 10

#define TRITON_MODE 0
#define URSULA_MODE 1

#define TRITONHUE 130
#define TRITONSAT 180
#define URSULAHUE 190
#define URSULASAT 170
#define ARIEL_HUE 255
#define ARIEL_SAT 200
#define TAIL_HUE 116
#define TAIL_SAT 200
#define LEGS_HUE 16
#define LEGS_SAT 200

#define BRIGHTNESS  200
#define MODE_LED_BRIGHTNESS 20

#define VELO_PIN_1_MIN 38
#define VELO_PIN_1_MAX 45
#define VELO_PIN_2_MIN 40
#define VELO_PIN_2_MAX 75
#define VELO_PIN_3_MIN 60
#define VELO_PIN_3_MAX 80

#define VELO_ARRAY_SIZE 120

#define FRAMES_PER_SECOND 60
#define FRAME_LIMIT 240

int veloArray[VELO_ARRAY_SIZE];
int veloArrayIndex = 0;

const int NUM_LEDS = NUM_SHAFT_LEDS + NUM_TINE1_LEDS + NUM_TINE2_LEDS + NUM_TINE3_LEDS + 1; // 1 for mode indicator
const int MODE_LED_LOCATION = NUM_SHAFT_LEDS ;  // location starts at 0
const int TINES_START = MODE_LED_LOCATION + 1;
const int TINES_TOTAL = NUM_TINE1_LEDS + NUM_TINE2_LEDS + NUM_TINE3_LEDS;

CHSV hsvs[NUM_LEDS];
CRGB leds[NUM_LEDS];

// BUTTON SETUP STUFF
byte prevAttackButtonState = HIGH;
byte prevMagicButtonState = HIGH;
byte prevModeButtonState = HIGH;

int sensorValue1 = 0;        // value read from the pressure pad
int sensorValue2 = 0;        // value read from the pressure pad
int sensorValue3 = 0;        // value read from the pressure pad

int framecount = 0;

int chaseRate = 0;
int priorChaseFrame = 0;     // previous frame at which we chased
int priorRing = 0;           // previous ring we lit (within each ring set)
int twinkleRate = 40;        // tine twinkling, bottom 4, top 40
int priorTwinkleFrame = 0;   // previous frame at which we twinkled

int currentMode = TRITON_MODE;

int magicMode = 0;
int magicColorCycle = 1;

int attackMode = 0;
int attackCounter = 0;

// we'll use this array to maintain, and average, total velo readings
void initializeVeloArray() {
  veloArrayIndex = 0;  // index to put latest reading into

  // initialize the array itself
  for( int i = 0; i < VELO_ARRAY_SIZE; i++) {
    veloArray[i] = 0;
  }
}

void setup() {
  delay( 2000 );              // power-up safety delay
  Serial.begin(57600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("Serial Connected");

  // setup pins
  pinMode(VELO_PIN_1, INPUT_PULLUP);
  pinMode(VELO_PIN_2, INPUT_PULLUP);
  pinMode(VELO_PIN_3, INPUT_PULLUP);
  initializeVeloArray();

  pinMode(ATTACK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MAGIC_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MODE_PIN, INPUT_PULLUP);

  pinMode(LED_BUILTIN, OUTPUT);

  // initial mode
  tritonMode();

  // initialize LEDs
  FastLED.addLeds<CHIPSET, DATAPIN, CLOCKPIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip );
  FastLED.setBrightness( BRIGHTNESS );

  Serial.println("ready");
}

void loop() {
  framecount += 1;

  // reset framecount
  if(framecount > FRAME_LIMIT) {
    framecount -= FRAME_LIMIT;
    priorChaseFrame -= FRAME_LIMIT;
    priorTwinkleFrame -= FRAME_LIMIT;
  }

  // get velo readings
  sensorValue1 = getVeloReading(VELO_PIN_1, VELO_PIN_1_MIN, VELO_PIN_1_MAX);
  sensorValue2 = getMappedVeloReading(VELO_PIN_2, VELO_PIN_2_MIN, VELO_PIN_2_MAX, 0, 30);
  sensorValue3 = getMappedVeloReading(VELO_PIN_3, VELO_PIN_2_MIN, VELO_PIN_3_MAX, 0, 30);

  // velo1 must be engaged to add other settings
  if(sensorValue1 > VELO_PIN_1_MIN) {
    // a reading of 2 gets us the minimal activity
    addVeloReading(2+sensorValue2+sensorValue3);
  } else {
    addVeloReading(0);
  }

  // adjust settings depending on velo readings
  adjustPower(averageVeloReading());

  // button management section
  byte currAttackButtonState = digitalRead(ATTACK_BUTTON_PIN);
  byte currMagicButtonState = digitalRead(MAGIC_BUTTON_PIN);

  if ((prevAttackButtonState == LOW) && (currAttackButtonState == HIGH)) {
  }
  if ((prevMagicButtonState == LOW) && (currMagicButtonState == HIGH)) {
    magicButtonRelease();
  }
  if ((prevAttackButtonState == HIGH) && (currAttackButtonState == LOW)) {
    attackButtonRelease();
  }
  if ((prevMagicButtonState == HIGH) && (currMagicButtonState == LOW)) {
    magicButtonPress();
  }
  prevAttackButtonState = currAttackButtonState;
  prevMagicButtonState = currMagicButtonState;

  byte currModeButtonState = digitalRead(MODE_PIN);
  if ((prevModeButtonState == LOW) && (currModeButtonState == HIGH)) {
    tritonMode();
  }
  if ((prevModeButtonState == HIGH) && (currModeButtonState == LOW)) {
    ursulaMode();
  }
  prevModeButtonState = currModeButtonState;

  updateShaftLeds();
  updateTineLeds();

  // convert HSV settings into the leds array
  hsv2rgb_rainbow( hsvs, leds, NUM_LEDS);

  // send updated led settings out to the physical leds
  FastLED.show();

  // wait until it's time for the next frame
  FastLED.delay(1000 / FRAMES_PER_SECOND);
}

// Read velostat, keeping results within the provided range
int getVeloReading(int pin, int minReading, int maxReading) {
  return min(max(100-analogRead(pin),minReading),maxReading);
}

// this one maps the actual reading to the provided range
int getMappedVeloReading(int pin, int minReading, int maxReading, int minValue, int maxValue) {
  int reading = getVeloReading(pin, minReading, maxReading);
  return map(reading, minReading, maxReading, minValue, maxValue);
}

void addVeloReading(int reading) {
  veloArray[veloArrayIndex] = reading;
  veloArrayIndex++;

  if(veloArrayIndex >= VELO_ARRAY_SIZE) {
    veloArrayIndex = 0;
  }
}

int averageVeloReading() {
  int sumVal = 0;

  for( int i = 0; i < VELO_ARRAY_SIZE; i++) {
    sumVal += veloArray[i];
  }

  return sumVal/VELO_ARRAY_SIZE;
}

// Easing functions
float cubicIn(float t) {
  return t*t*t;
}

float cubicOut(float t) {
  return 1-cubicIn(1-5);
}

int easeInOutMap(int val, int lowVal, int highVal, int lowRange, int highRange) {
  int inRange = highVal - lowVal;
  int outRange = highRange - lowRange;
  val = max(val,lowVal);
  val = min(val,highVal);
  float t = (val-lowVal)/(inRange * 1.0);
  float r;
  if(t < 0.5) {
    r = cubicIn(t*2.0)/2.0;
  } else {
    r = 1-cubicIn((1-t)*2)/2;
  }
  return (r * outRange) + lowRange;
}

//BUTTON CONTROL STUFF

// called when key goes from pressed to not pressed
void attackButtonRelease() {
  Serial.println("attack button released");
  startAttack();
}

void magicButtonPress() {
  Serial.println("magic button pressed");
  startMagic();
}
void magicButtonRelease() {
  Serial.println("magic button released");
  endMagic();
}

void tritonMode() {
  Serial.println("triton");
  currentMode = TRITON_MODE;

  // set all shaft leds with triton color
  setHS(TRITONHUE, TRITONSAT, 100, 0, NUM_SHAFT_LEDS);

  // set mode indicator led
  hsvs[MODE_LED_LOCATION] = CHSV(TRITONHUE, TRITONSAT,MODE_LED_BRIGHTNESS);
}

void ursulaMode() {
  Serial.println("ursula");
  currentMode = URSULA_MODE;

  // set most shaft leds with ursula color, with a chance of just desaturating what's already there (triton hue)
  setHS(URSULAHUE, URSULASAT, 70, 0, NUM_SHAFT_LEDS);

  // set mode indicator led
  hsvs[MODE_LED_LOCATION] = CHSV(URSULAHUE, URSULASAT,MODE_LED_BRIGHTNESS);
}

void setHS(int hue, int sat, int pct, int start, int end) {
  for( int i = start; i < end; i++) {
    if(pct == 100 || random8(100) < pct) {
      hsvs[i].hue = hue;
      hsvs[i].sat = sat;
    } else {
      hsvs[i].sat = sat * 0.7;
    }
  }
}

int topPressure = 60;   // max velo sum
int bottomPressure = 0; // min velo sum

int decay = 1;         // amount we'll decay per frame
int minBright = 0;     // we won't decay below this
int topBright = 100;   // we won't go any brighter on shaft
int topTineBright = 0; // we won't go any brighter on tines
int tineProb = 0;      // probability of tine lighting
int tineDecay = 3;     // tine decay rate


// adjust values based on current average velo reading
void adjustPower(int pressure) {
  if(pressure > bottomPressure) {
    decay = map(pressure, bottomPressure, topPressure, 1, 15);
    minBright = easeInOutMap(pressure, bottomPressure, topPressure, 25, 80);
    topBright = easeInOutMap(pressure, bottomPressure, topPressure, 40, 255);
    chaseRate = easeInOutMap(pressure, bottomPressure, topPressure, 5, 20);
    topTineBright = easeInOutMap(pressure, bottomPressure, topPressure, 20, 150);
    twinkleRate = easeInOutMap(pressure, bottomPressure, topPressure, 0, 40);
    tineProb = easeInOutMap(pressure, bottomPressure, topPressure, 30, 200);
    tineDecay = easeInOutMap(pressure, bottomPressure, topPressure, 0, 3);
  } else {
    decay = 10;
    minBright = 0;
    topBright = 0;
    chaseRate = 0;
    topTineBright = 0;
    twinkleRate = 0;
    tineProb = 0;
    tineDecay =  0;
  }
  if(magicMode) {
    chaseRate = 10;
    topBright = 190;
    decay = 15;
    minBright = 0;
  }
}

void startAttack() {
  attackMode = 1;
  attackCounter = 0;
}

void startMagic() {
  magicMode = 1;
  magicColorCycle = 0;
}

void endMagic() {
  magicMode = 0;
  if(currentMode == TRITON_MODE) {
    tritonMode();
  } else {
    ursulaMode();
  }
}

void updateShaftLeds() {
  // decay all leds
  for( int i = 0; i < NUM_SHAFT_LEDS; i++) {
    hsvs[i].val = max(hsvs[i].val - decay, minBright);
  }

  int newHue = 0;
  int newSat = 0;

  // update if this is a chase frame
  if(framecount > (priorChaseFrame + FRAMES_PER_SECOND/chaseRate)) {
    // update the chase ring (of each set)
    priorRing += 1;
    if(priorRing > 4) {
      priorRing = 0;
    }

    if(magicMode) {
      switch(magicColorCycle) {
        case 1:
          newHue = ARIEL_HUE;
          newSat = ARIEL_SAT;
          magicColorCycle = 2;
          break;
        case 2:
          newHue = TAIL_HUE;
          newSat = TAIL_SAT;
          magicColorCycle = 3;
          break;
        case 3:
        default:
          newHue = LEGS_HUE;
          newSat = LEGS_SAT;
          magicColorCycle = 1;
        break;
      }
    }

    // each ring has 4 LEDs - update all sets on each side
    for (int i = 0; i < 4; i++) {
      hsvs[(priorRing)*4+i].val = topBright;     // first set
      hsvs[(priorRing+5)*4+i].val = topBright;   // second
      hsvs[(priorRing+10)*4+i].val = topBright;  // third
      if(magicMode) {  // we change colors in this mode
        hsvs[(priorRing)*4+i].hue = newHue;
        hsvs[(priorRing+5)*4+i].hue = newHue;
        hsvs[(priorRing+10)*4+i].hue = newHue;
        hsvs[(priorRing)*4+i].sat = newSat;
        hsvs[(priorRing+5)*4+i].sat = newSat;
        hsvs[(priorRing+10)*4+i].sat = newSat;
      }
    }
    priorChaseFrame = framecount;
  }
}

void updateTineLeds() {
  int ring;

  if(attackMode) { // big flash of the tine LEDs
    attackCounter += 1;
    if(attackCounter > 50) { // off
      attackMode = 0;
      for( int i = 0; i < (TINES_TOTAL); i++) {
        hsvs[i+TINES_START].val = 0;
      }
    } else if(attackCounter > 40) { // quick cooldown
      for( int i = 0; i < (TINES_TOTAL); i++) {
        hsvs[i+TINES_START].val = max(hsvs[i+TINES_START].val -20, 0);
      }
    } else if(attackCounter > 30) { // blast
      for( int i = 0; i < (TINES_TOTAL); i++) {
        hsvs[i+TINES_START].val = min(hsvs[i+TINES_START].val + 12, 255);
      }
    } else { // initial attack
      for( int i = 0; i < (TINES_TOTAL); i++) {
        hsvs[i+TINES_START].val = min(hsvs[i+TINES_START].val + 6, 255);
      }
    }
  } else {
    // decay all
    for( int i = 0; i < (TINES_TOTAL); i++) {
      hsvs[i+TINES_START].val = max(hsvs[i+TINES_START].val - (5-tineDecay), 0);
    }

    // twinkle
    if(framecount > priorTwinkleFrame + FRAMES_PER_SECOND/twinkleRate) {
      priorTwinkleFrame = framecount;
      if(random8() < tineProb) {
        hsvs[ TINES_START + random16(NUM_TINE1_LEDS) ].val = topTineBright;
      }
      if(random8() < tineProb) {
        hsvs[ TINES_START + NUM_TINE1_LEDS + random16(NUM_TINE2_LEDS) ].val = topTineBright;
      }
      if(random8() < tineProb) {
        hsvs[ TINES_START + NUM_TINE1_LEDS + NUM_TINE2_LEDS + random16(NUM_TINE3_LEDS) ].val = topTineBright;
      }
    }
  }
}
