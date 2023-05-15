#include <Arduino.h>

// https://github.com/FastLED/FastLED/wiki/Overview
#include <FastLED.h>

// Defined for Arduino Micro
#define DATAPIN    16  // COPI/PICO
#define CLOCKPIN   15  // SCK

#define ATTACK_BUTTON_PIN 5
#define MAGIC_BUTTON_PIN 4
#define MODE_BUTTON_PIN 6

#define INTENSITY_CTRL_PIN A4  // Linear potentiometer (fader) for smooth, clean control of intensity.
#define INTENSITY_CTRL_5V A5  // One end of the potentiometer needs 5V. This can be provided by a digital pin if desired.
#define INTENSITY_CTRL_0V A3  // The other end needs GND/OV. Using digital pins allows for flipping polarity.
#define INTENSITY_CTRL_MIN_OUTPUT 18  // The minimum value returned by analogRead(INTENSITY_CTRL_PIN) (i.e. when fader is all the way down).
#define INTENSITY_CTRL_MAX_OUTPUT 1023  // The maximum value returned by analogRead(INTENSITY_CTRL_PIN).

#define COLOR_ORDER BGR  // Test this using the Blink.ino example from FastLED
#define CHIPSET     DOTSTAR  // AKA APA102, come highly recommended: https://github.com/FastLED/FastLED/wiki/Chipset-reference

// NUM_SHAFT_LEDS / LEDS_PER_RING and NUM_SHAFT_LEDS / RINGS_PER_SET must be
// whole numbers, i.e. LEDS_PER_RING and NUM_SHAFT_LEDS must be factors of
// NUM_SHAFT_LEDS.
#define NUM_SHAFT_LEDS 150
#define LEDS_PER_RING 5  // Used for computing chase effects. It's fine if this number is odd.
#define RINGS_PER_SET 10  // Used to space out the ring chases (so it's not a single chase up the length of the shaft).
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

// FIFO queue size for averaging out fader input to slow responsiveness.
#define INTENSITY_CTRL_FILTER_LENGTH 30

#define FRAMES_PER_SECOND 60
#define FRAME_LIMIT 240

// The 'intensity' helps indicate the mood in the room. Angry triton has a more
// intense trident (faster, more common, brighter twinkling).
int intensityCtrlFilter[INTENSITY_CTRL_FILTER_LENGTH];
// By modifying the part of the array that we touch each cycle, the queue is
// FIFO without having to actually move any data in memory.
int intensityCtrlFilterIndex = 0;

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
// Got a complaint about this not being defined in scope, so I put a header
// above. Probably something to do with the function pointers in the arg list.
bool readButtonAndAct(byte buttonPin, bool previousState, void (*buttonPressCallback)(), void (*buttonReleaseCallback)());

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
unsigned long attackStart = 0;

/**
 * @brief Fill the filter with zero values as a starting point.
 */
void initializeIntensityCtrlFilter() {
  intensityCtrlFilterIndex = 0;  // index to put latest reading into

  // initialize the array itself
  for(int i = 0; i < INTENSITY_CTRL_FILTER_LENGTH; i++) {
    intensityCtrlFilter[i] = 0;
  }
}

void setup() {
  delay( 2000 );  // power-up safety delay
  Serial.begin(57600);

  // setup pins
  pinMode(INTENSITY_CTRL_PIN, INPUT_PULLUP);
  pinMode(INTENSITY_CTRL_5V, OUTPUT); digitalWrite(INTENSITY_CTRL_5V, HIGH);
  pinMode(INTENSITY_CTRL_0V, OUTPUT); digitalWrite(INTENSITY_CTRL_0V, LOW);
  initializeIntensityCtrlFilter();

  pinMode(ATTACK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MAGIC_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);

  // pinMode(LED_BUILTIN, OUTPUT);

  // initial mode
  tritonMode();

  // initialize LEDs
  FastLED.addLeds<CHIPSET, DATAPIN, CLOCKPIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  Serial.println("ready");
}

void loop() {
  prevAttackButtonState = readButtonAndAct(ATTACK_BUTTON_PIN, prevAttackButtonState, &startAttack, &no_op);
  prevMagicButtonState = readButtonAndAct(MAGIC_BUTTON_PIN, prevMagicButtonState, &startMagic, &endMagic);
  prevModeButtonState = readButtonAndAct(MODE_BUTTON_PIN, prevModeButtonState, &ursulaMode, &tritonMode);

  framecount += 1;

  // reset framecount
  if(framecount > FRAME_LIMIT) {
    framecount -= FRAME_LIMIT;
    priorChaseFrame -= FRAME_LIMIT;
    priorTwinkleFrame -= FRAME_LIMIT;
  }

  pushIntensityCtrlValueToFilter(analogRead(INTENSITY_CTRL_PIN));
  adjustPower(getAverageIntensityCtrlValue());

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
 * @brief Read the button state, compare to previous state, then act.
 *
 * @param buttonPin The digital pin to read. Assumes the mode is INPUT_PULLUP
 * @param previousState The last measured state of the button.
 * @param buttonPressCallback Zero-arg function to call upon a rising edge.
 * @param buttonReleaseCallback Zero-arg function to call upon a falling edge.
 * @return current button state, for passing through next time.
 */
bool readButtonAndAct(
  byte buttonPin,
  bool previousState,
  void (*buttonPressCallback)(),
  void (*buttonReleaseCallback)()
) {
  byte currentState = digitalRead(buttonPin);
  // Only checking for rising (press) or falling (release) edges.
  if (previousState == currentState) return currentState;

  // Falling edge happens when current state is HIGH because of the pullup
  // resistor applied by INPUT_PULLUP.
  if (currentState == LOW) {
    buttonPressCallback();
  } else {
    buttonReleaseCallback();
  }

  return currentState;
}

/// @brief No-operation for button callbacks.
void no_op(){;}

/**
 * @brief Put `reading` into a queue for filtering (averaging) readings from the
 * sensor(s).
 *
 * @param reading Value to add to the queue
 */
void pushIntensityCtrlValueToFilter(int reading) {
  intensityCtrlFilter[intensityCtrlFilterIndex] = reading;
  intensityCtrlFilterIndex++;

  if(intensityCtrlFilterIndex >= INTENSITY_CTRL_FILTER_LENGTH) {
    intensityCtrlFilterIndex = 0;
  }
}

/**
 * @brief Return the average of intensityCtrlFilter.
 *
 * Note that this value will be truncated toward zero due to integer division.
 *
 * @return int
 */
int getAverageIntensityCtrlValue() {
  int sumVal = 0;

  for( int i = 0; i < INTENSITY_CTRL_FILTER_LENGTH; i++) {
    sumVal += intensityCtrlFilter[i];
  }

  return sumVal/INTENSITY_CTRL_FILTER_LENGTH;
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


int decay = 1;         // Rate at which leds dim each iteration of loop()
int minBright = 0;     // we won't decay below this
int topBright = 100;   // we won't go any brighter on shaft
int topTineBright = 0; // we won't go any brighter on tines
int tineProb = 0;      // probability of tine lighting
int tineDecay = 3;     // tine decay rate


/**
 * @brief Adjust values based on current average fader value.
 *
 * @param intensity Setting provided by the fader to set twinkle intensity.
 */
void adjustPower(int intensity) {
  if(intensity > INTENSITY_CTRL_MIN_OUTPUT) {
    decay = map(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 1, 15);
    minBright = easeInOutMap(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 25, 80);
    topBright = easeInOutMap(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 40, 255);
    chaseRate = easeInOutMap(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 5, 20);
    topTineBright = easeInOutMap(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 20, 150);
    twinkleRate = easeInOutMap(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 0, 40);
    tineProb = easeInOutMap(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 30, 200);
    tineDecay = easeInOutMap(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 0, 3);
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
  attackStart = millis();
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
  if (attackMode) {
    attack();
    return;
  }

  // decay all
  for (int i = 0; i < (TINES_TOTAL); i++) {
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

void attack() {
  // Use piecewise cosines to fade in fast and fade out slower.
  unsigned long timeInAttack = millis() - attackStart;
  byte brightness;
  // cos8 is a 0-255 function, so Period = 1 means it'll start at peak at 0 ms
  // and peak again at 255 ms. To get a ~1 second effect, we need to stretch
  // the period out to 4 times the original size. To position the peaks
  // correctly, we use a time offset. cos((t - offset) / period).
  if (timeInAttack < 255) {
    // The period is 2, so the trough happens 255 ms into the curve.
    // Subtracting 255 starts us there at t=0
    brightness = cos8((timeInAttack - 255) / 2);
  } else if (timeInAttack < 1020) {
    // The cosine starts at its peak. It starts when t = 255 ms, so subtract
    // that offset to start at the peak. A period of 6 means the wave troughs
    // at 1020 seconds.
    brightness = cos8((timeInAttack - 255) / 6);
  } else {
    brightness = 0;
    attackMode = 0;
  }
  for( int i = 0; i < (TINES_TOTAL); i++) {
    hsvs[i+TINES_START].val = brightness;
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
