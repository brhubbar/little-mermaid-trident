#include <Arduino.h>

// https://github.com/FastLED/FastLED/wiki/Overview
#include <FastLED.h>

// Defined for Arduino Micro
#define DATAPIN    16  // COPI/PICO
#define CLOCKPIN   15  // SCK

#define ATTACK_BUTTON_PIN 5
#define MAGIC_BUTTON_PIN 4
#define MODE_PIN 6
#define VELO_PIN_1 A5  // holding sensor
#define VELO_PIN_2 A4  // secondary pressure
#define VELO_PIN_3 A3  // tertiary pressure
#define GREENWIRE A5  // holding sensor
#define BLUEWIRE A4  // secondary pressure
#define REDWIRE A3  // tertiary pressure

#define COLOR_ORDER BGR  // Test this using the Blink.ino example from FastLED
#define CHIPSET     DOTSTAR  // AKA APA102, come highly recommended: https://github.com/FastLED/FastLED/wiki/Chipset-reference
// NUM_SHAFT_LEDS / LEDS_PER_RING and NUM_SHAFT_LEDS / RINGS_PER_SET must be
// whole numbers, i.e. LEDS_PER_RING and NUM_SHAFT_LEDS must be factors of
// NUM_SHAFT_LEDS.
#define NUM_SHAFT_LEDS 150
#define LEDS_PER_RING 5  // Used for computing chase effects. It's fine if this number is odd.
#define RINGS_PER_SET 6  // Used to space out the ring chases (so it's not a single chase up the length of the shaft).
#define NUM_TINE1_LEDS 19  // 10
#define NUM_TINE2_LEDS 19  // 10
#define NUM_TINE3_LEDS 19  // 10

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

// Velostat (pressure sensitive analog input) configuration.
#define VELO_PIN_1_MIN 38
#define VELO_PIN_1_MAX 45
#define VELO_PIN_2_MIN 40
#define VELO_PIN_2_MAX 75
#define VELO_PIN_3_MIN 60
#define VELO_PIN_3_MAX 80

#define VELO_ARRAY_SIZE 30

#define FRAMES_PER_SECOND 60
#define FRAME_LIMIT 240

int veloQueue[VELO_ARRAY_SIZE];
int veloQueueIndex = 0;

const int NUM_LEDS = NUM_SHAFT_LEDS + NUM_TINE1_LEDS + NUM_TINE2_LEDS + NUM_TINE3_LEDS + 1; // 1 for mode indicator
const int NUM_RING_SETS = NUM_SHAFT_LEDS / LEDS_PER_RING / RINGS_PER_SET;
const int MODE_LED_LOCATION = NUM_SHAFT_LEDS ;  // location starts at 0, mode led is between shaft and tines.
const int TINES_START = MODE_LED_LOCATION + 1;
const int TINES_TOTAL = NUM_TINE1_LEDS + NUM_TINE2_LEDS + NUM_TINE3_LEDS;

CHSV hsvs[NUM_LEDS];
CRGB leds[NUM_LEDS];

// BUTTON SETUP STUFF
byte prevAttackButtonState = HIGH;
byte prevMagicButtonState = HIGH;
byte prevModeButtonState = HIGH;

int veloValue1 = 0;        // value read from the pressure pad
int veloValue2 = 0;        // value read from the pressure pad
int veloValue3 = 0;        // value read from the pressure pad

int framecount = 0;

int chaseRate = 0;
int priorChaseFrame = 0;     // previous frame at which we chased
int currentRingInSet = 0;    // Within a set of rings, which one is flashing? (For a chase)
int twinkleRate = 40;        // tine twinkling, bottom 4, top 40
int priorTwinkleFrame = 0;   // previous frame at which we twinkled

int currentMode = TRITON_MODE;

int magicMode = 0;
int magicColorCycle = 1;

int attackMode = 0;
int attackCounter = 0;

// we'll use this array to maintain, and average, total velo readings
void initializeVeloArray() {
  veloQueueIndex = 0;  // index to put latest reading into

  // initialize the array itself
  for( int i = 0; i < VELO_ARRAY_SIZE; i++) {
    veloQueue[i] = 0;
  }
}

void setup() {
  delay( 2000 );              // power-up safety delay
  // Serial.begin(57600);
  // while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  // }
  // Serial.println("Serial Connected");

  // setup pins
  pinMode(REDWIRE, OUTPUT);
  digitalWrite(REDWIRE, LOW);
  pinMode(GREENWIRE, OUTPUT);
  digitalWrite(GREENWIRE, HIGH);
  pinMode(BLUEWIRE, INPUT_PULLUP);
  // pinMode(VELO_PIN_1, INPUT_PULLUP);
  // pinMode(VELO_PIN_2, INPUT_PULLUP);
  // pinMode(VELO_PIN_3, INPUT_PULLUP);
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
  FastLED.clear();
  FastLED.show();

  // ursulaMode();
  // startMagic();

  // Serial.println("ready");
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
  veloValue1 = getVeloValue(BLUEWIRE, VELO_PIN_1_MIN, VELO_PIN_1_MAX);
  veloValue2 = 0; // getMappedVeloValue(VELO_PIN_2, VELO_PIN_2_MIN, VELO_PIN_2_MAX, 0, 30);
  veloValue3 = 0; // getMappedVeloValue(VELO_PIN_3, VELO_PIN_2_MIN, VELO_PIN_3_MAX, 0, 30);

  pushVeloValueToQueue(veloValue1);
  // velo1 must be engaged to add other settings
  // if(veloValue1 > VELO_PIN_1_MIN) {
  //   // a reading of 2 gets us the minimal activity
  //   pushVeloValueToQueue(2+veloValue2+veloValue3);
  // } else {
  //   pushVeloValueToQueue(0);
  // }

  // adjust settings depending on velo readings
  adjustPower(getAverageVeloValue());

  // button management section
  byte currAttackButtonState = digitalRead(ATTACK_BUTTON_PIN);
  byte currMagicButtonState = digitalRead(MAGIC_BUTTON_PIN);

  if ((prevAttackButtonState == LOW) && (currAttackButtonState == HIGH)) {
    // No-Op, we're waiting for a falling edge on the attack button.
  }
  if ((prevAttackButtonState == HIGH) && (currAttackButtonState == LOW)) {
    attackButtonRelease();
  }
  if ((prevMagicButtonState == LOW) && (currMagicButtonState == HIGH)) {
    magicButtonRelease();
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


/**
 * @brief Read velostat, keeping results within the provided range.
 *
 * Biases the analogRead down by 100, which presumably represents the
 * noise floor of the setup used in the original implementation.
 *
 * TODO: This may need to change.
 *
 * @param pin Analog pin to read from.
 * @param minReading Minimum value returned.
 * @param maxReading Maximum value returned.
 * @return int Windowed return value.
 */
int getVeloValue(int pin, int minReading, int maxReading) {
  // analogRead returns [0,1023]
  // 100-analogRead == [-100,923]
  // max([-100,923], 38) returns [38,923]
  // min([38,923], 45) returns [38,45]
  // return min(max(100-analogRead(pin),minReading),maxReading);
  int ret = map(analogRead(pin),0, 1023, 0 ,62);
  // Serial.println(ret);
  return ret;
}

/**
 * @brief Read velostat, mapping the actual reading to the provided range.
 *
 * @param pin Analog pin to read from.
 * @param minReading Minimum value expected from getVeloValue.
 * @param maxReading Maximum value expected from getVeloValue.
 * @param minValue Minimum value returned.
 * @param maxValue Maximum value returned.
 * @return int Windowed return value.
 */
int getMappedVeloValue(int pin, int minReading, int maxReading, int minValue, int maxValue) {
  int reading = getVeloValue(pin, minReading, maxReading);
  return map(reading, minReading, maxReading, minValue, maxValue);
}

/**
 * @brief Put `reading` into a queue for filtering (averaging) readings from the
 * sensor(s).
 *
 * @param reading Value to add to the queue
 */
void pushVeloValueToQueue(int reading) {
  veloQueue[veloQueueIndex] = reading;
  veloQueueIndex++;

  if(veloQueueIndex >= VELO_ARRAY_SIZE) {
    veloQueueIndex = 0;
  }
}

/**
 * @brief Return the average of veloQueue.
 *
 * Note that this value will be truncated toward zero due to integer division.
 *
 * @return int
 */
int getAverageVeloValue() {
  int sumVal = 0;

  for( int i = 0; i < VELO_ARRAY_SIZE; i++) {
    sumVal += veloQueue[i];
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

/**
 * @brief Map a value to a cubic curve.
 *
 * @details
 *   - Clips `val` between `lowVal` and `highVal`.
 *   -
 *
 * @param val
 * @param lowVal
 * @param highVal
 * @param lowRange
 * @param highRange
 * @return int
 */
int easeInOutMap(int val, int lowVal, int highVal, int lowRange, int highRange) {
  int inRange = highVal - lowVal;
  int outRange = highRange - lowRange;

  // Clip the input value.
  val = max(val,lowVal);
  val = min(val,highVal);

  //
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
  // Serial.println("attack button released");
  startAttack();
}

void magicButtonPress() {
  // Serial.println("magic button pressed");
  startMagic();
}
void magicButtonRelease() {
  // Serial.println("magic button released");
  endMagic();
}

void tritonMode() {
  // Serial.println("triton");
  currentMode = TRITON_MODE;

  // set all shaft leds with triton color
  setHS(TRITONHUE, TRITONSAT, 100, 0, NUM_SHAFT_LEDS);

  // set mode indicator led
  hsvs[MODE_LED_LOCATION] = CHSV(TRITONHUE, TRITONSAT, MODE_LED_BRIGHTNESS);
}

void ursulaMode() {
  // Serial.println("ursula");
  currentMode = URSULA_MODE;

  // set most shaft leds with ursula color, with a chance of just desaturating what's already there (triton hue)
  setHS(URSULAHUE, URSULASAT, 70, 0, NUM_SHAFT_LEDS);

  // set mode indicator led
  hsvs[MODE_LED_LOCATION] = CHSV(URSULAHUE, URSULASAT, MODE_LED_BRIGHTNESS);
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

int decay = 1;         // Rate at which leds dim each iteration of loop()
int minBright = 0;     // we won't decay below this
int topBright = 100;   // we won't go any brighter on shaft
int topTineBright = 0; // we won't go any brighter on tines
int tineProb = 0;      // probability of tine lighting
int tineDecay = 3;     // tine decay rate


/**
 * @brief Adjust values based on current average velo reading.
 *
 * @param pressure Average velo reading representing squeeze pressure on the
 * trident.
 */
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

  // Stop if a chase 'frame' happened too recently.
  if(framecount < (priorChaseFrame + FRAMES_PER_SECOND/chaseRate)) {
    return;
  }
  // Rings are grouped together into sets of four.
  currentRingInSet += 1;
  if(currentRingInSet >= RINGS_PER_SET) {
    currentRingInSet = 0;
  }

  // Cycle through ariel / sea / human colors for the magic effect.
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

  // Update each led in the current ring in each set.
  //
  // My LEDs are snaking (see end of file for illustrations), so addressing is a
  // little more mathematically intensive. Each configuration I've considered is
  // laid out below, with snaking actually implemented. Note that snaking LEDs
  // assumes that LEDS_PER_RING is an even number.
  //
  // LED addresses for ring n in snaking configuration:
  //     0*2*NUM_SHAFT_LEDS/LEDS_PER_RING+n
  //     1*2*NUM_SHAFT_LEDS/LEDS_PER_RING-1-n
  //     1*2*NUM_SHAFT_LEDS/LEDS_PER_RING+n
  //     2*2*NUM_SHAFT_LEDS/LEDS_PER_RING-1-n
  //     ...
  // LED addresses for ring n in climbing configuration:
  //     0*2*NUM_SHAFT_LEDS/LEDS_PER_RING+n
  //     1*2*NUM_SHAFT_LEDS/LEDS_PER_RING+n
  //     2*2*NUM_SHAFT_LEDS/LEDS_PER_RING+n
  //     3*2*NUM_SHAFT_LEDS/LEDS_PER_RING+n
  //     ...
  // LED addresses for ring n in wrapping configuration:
  //     0 + LEDS_PER_RING*n
  //     1 + LEDS_PER_RING*n
  //     2 + LEDS_PER_RING*n
  //     3 + LEDS_PER_RING*n
  //     ...
  // n == currentRingInSet + an offset for which set of rings we're updating.

  // Uncomment all of the Serial.prints to get troubleshooting output to review
  // what happens in a given loop.
  // Serial.print("\n\nRing");
  // Serial.println(currentRingInSet);
  for (int setIdx = 0; setIdx < NUM_RING_SETS; setIdx++) {
    for (int ledIdx = 0; ledIdx < LEDS_PER_RING; ledIdx++) {
      // We have LEDs snaking up and down, swap between the two with each
      // iteration using modulo. ledIdxDiv2 accounts for the numbering pattern
      // noted in the long comment above for snaking configuration.
      int ledIdxDiv2 = ceil(ledIdx / 2);
      int ledAddr = 0;
      if (ledIdx % 2 == 0) {
        // Even, ascending
        ledAddr = (ledIdxDiv2)*(2*NUM_SHAFT_LEDS/LEDS_PER_RING) + (currentRingInSet + setIdx*RINGS_PER_SET);
      } else {
        // Odd, descending
        ledAddr = (ledIdxDiv2+1)*(2*NUM_SHAFT_LEDS/LEDS_PER_RING) - (currentRingInSet + setIdx*RINGS_PER_SET) - 1;
      }
      // Serial.print("Asc: ");
      // Serial.print(ascendingLedAddr);
      // Serial.print(", Desc: ");
      // Serial.print(descendingLedAddr);
      hsvs[ledAddr].val = topBright;
      if(magicMode) {  // we change colors in this mode
        hsvs[ledAddr].hue = newHue;
        hsvs[ledAddr].sat = newSat;
      }
    }
    // Serial.println();
  }
  priorChaseFrame = framecount;
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

// Snaking LEDs:
// ....  ....
// |  |  |  |
// |  |  |  |
// |  |  |  |
// |  |  |  |
// |  |  |  |
// |  |  |  |
// |  |  |  |
// |  |  |  |
// |  |  |  |
// |  |  |  |
// |  |  |  |
// |  |  |  |
// |  ....  |
//
// climbing LEDs:
// |. |. |. |
// |. |. |. |
// |. |. |. |
// |. |. |. |
// |. |. |. |
// |. |. |. |
// |. |. |. |
// | .| .| .|
// | .| .| .|
// | .| .| .|
// | .| .| .|
// | .| .| .|
// | .| .| .|
// | .| .| .|
//
// Wrapping LEDs:
// __________
// .....
//      .....
// __________
// .....
//      .....
// __________
// .....
//      .....
// __________
// .....
//      .....
// __________
//
