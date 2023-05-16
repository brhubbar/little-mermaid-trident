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
#define RINGS_PER_SET 6  // Used to space out the ring chases (so it's not a single chase up the length of the shaft).
#define NUM_TINE1_LEDS 33  // 10
#define NUM_TINE2_LEDS 13  // 10
#define NUM_TINE3_LEDS 13  // 10

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
const int NUM_RINGS = NUM_SHAFT_LEDS / LEDS_PER_RING;
const int NUM_RING_SETS = NUM_RINGS / RINGS_PER_SET;

const int SHAFT_START_ADDR = 0;
const int SHAFT_LAST_ADDR = NUM_SHAFT_LEDS - 1;
const int MODE_LED_LOCATION = NUM_SHAFT_LEDS ;  // location starts at 0, mode led is between shaft and tines.
const int TINES_TOTAL = NUM_TINE1_LEDS + NUM_TINE2_LEDS + NUM_TINE3_LEDS;
const int TINES_START_ADDR = MODE_LED_LOCATION + 1;
const int TINES_LAST_ADDR = TINES_START_ADDR + TINES_TOTAL;

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
  adjustTwinkleIntensity(getAverageIntensityCtrlValue());

  // shaftChase();
  if (magicMode) {
    magicShaftChase();
    twinkle(TINES_START_ADDR, TINES_LAST_ADDR, 0);
  } else if (attackMode) {
    attackTinesFlash();
    attackShaftChase();
  } else {
    twinkle(TINES_START_ADDR, TINES_LAST_ADDR, 0);
    twinkle(SHAFT_START_ADDR, SHAFT_LAST_ADDR, 0);
  }

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

// Fixing the duty cycle range helps keep power consumption in check. Varying
// the frequency affects the intensity/francticness of the mood, so varying the
// duty cycle as well would be turning two knobs to control the same effect, and
// therefore counterproductive.
const uint16_t twinkleDutyCycleMin = 2500;  // 2.5% of the time, the led is on.
const uint16_t twinkleDutyCycleMax = 5000;  // 5% of the time, the led is on.
const uint16_t twinkleBrightnessMin = 0;  // This knob is made available but I don't think its desireable.
uint16_t twinkleBrightnessMax = 100;  // Brighter twinkling leads to a stronger mood.
// These have been fixed in time because I don't have time to work out the math
// for phase-aligning each led as its frequency changes. The problem (brightness
// is the 'signal') and its solution are captured beautifully on stack exchange:
// https://dsp.stackexchange.com/q/76284
const uint16_t twinkleFrequencyMin = 2000;  // Range from 2000 to 6000 to yield maximum period of 30 to 11 seconds, respectively.
const uint16_t twinkleFrequencyMax = 4000;  // Range from 4000 to 32000 to yield a minimum period of 16 to 2 seconds, respectively.

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
void adjustTwinkleIntensity(int intensity) {
  twinkleBrightnessMax = map(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 0, 200);
  // Disabled; see note at the definition of these variables above.
  // twinkleFrequencyMin = map(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 2000, 6000);
  // twinkleFrequencyMax = map(intensity, INTENSITY_CTRL_MIN_OUTPUT, INTENSITY_CTRL_MAX_OUTPUT, 4000, 32000);

  if(magicMode) {
    chaseRate = 10;
    topBright = 190;
    decay = 10;
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

void magicShaftChase() {
  // decay all leds
  for( int i = 0; i < NUM_SHAFT_LEDS; i++) {
    hsvs[i].val = max(hsvs[i].val - decay, minBright);
  }

  // Stop if a chase 'frame' happened too recently.
  if(framecount < (priorChaseFrame + FRAMES_PER_SECOND/chaseRate)) {
    return;
  }
  // Rings are grouped together into sets of four.
  currentRingInSet += 1;
  if(currentRingInSet >= RINGS_PER_SET) {
    currentRingInSet = 0;
  }

  int newHue;
  int newSat;
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
  for (int setIdx = 0; setIdx < NUM_RING_SETS; setIdx++) {
    for (int ledIdx = 0; ledIdx < LEDS_PER_RING; ledIdx++) {
      // We have LEDs snaking up and down, swap between the two with each
      // iteration using modulo. ledIdxDiv2 accounts for the numbering pattern
      // noted in the long comment above for snaking configuration.
      int ledAddr = getLedAddrInRing(setIdx*RINGS_PER_SET, ledIdx);
      hsvs[ledAddr].val = topBright;
      hsvs[ledAddr].hue = newHue;
      hsvs[ledAddr].sat = newSat;
    }
    // Serial.println();
  }
  priorChaseFrame = framecount;
}

/**
 * @brief Get the Led Addr In Ring object
 *
 * @param ringIdx [0, NUM_RINGS) id for the ring, counting from the bottom of the shaft.
 * @param ledIdxInRing [0, LEDS_PER_RING) Id for the led in the ring
 * @return int Address of the led.
 */
int getLedAddrInRing(int ringIdx, int ledIdxInRing) {
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

  // We have LEDs snaking up and down, swap between the two with each
  // iteration using modulo. ledIdxDiv2 accounts for the numbering pattern
  // noted in the long comment above for snaking configuration.
  int ledIdxDiv2 = ceil(ledIdxInRing / 2);
  if (ledIdxInRing % 2 == 0) {
    // Even, ascending
    return (ledIdxDiv2)*(2*NUM_SHAFT_LEDS/LEDS_PER_RING) + (currentRingInSet + ringIdx);
  } else {
    // Odd, descending
    return (ledIdxDiv2+1)*(2*NUM_SHAFT_LEDS/LEDS_PER_RING) - (currentRingInSet + ringIdx) - 1;
  }
}

/**
 * @brief Apply a pseudo random twinkle to a range of leds. Twinkle behavior is
 * deterministic for a constant range of leds.
 *
 * The knobs to turn are twinkleFrequencyMin, twinkleFrequencyMax,
 * twinkleBrightnessMin, and twinkleBrightnessMax. Higher frequencies and higher
 * brightnesses lead to a more intense mood.
 *
 * @param firstLedAddr LED to start at
 * @param lastLedAddr Last LED to update
 * @param skip_n_leds [0-n] Skip every so many LEDs to save on computation time.
 */
void twinkle(int firstLedAddr, int lastLedAddr, int skip_n_leds) {
  // Inspired by
  // https://gist.github.com/atuline/02e71a57636498d382e276311b328e53,
  // twinklefox, etc.
  //
  // The idea is to use sine waves to fade the leds in and out. Each LED is
  // assigned a pseudo-random frequency so they don't all turn on/off at the
  // same time or rate. Using a wave plus a vertical offset makes it possible to
  // clip so that ights have two states - fade in/out, or off, and the
  // frequency of fade in/outs is controllable by adjusting the vertical offset.
  // The time on vs off is referred to as duty cycle. Duty cycle can be
  // approximated by vertically offsetting proportional to the range of the
  // wave (e.g. 127 offset on a sin8 would give a 50% duty cycle.) To convert
  // that into brightness values, we'll use the scale functions.
  //
  // LO INTENSITY: fade in/out period of 3-4 seconds; duty cycle 10-25% fading.
  // HI INTENSITY: fade in/out period of 0.5-1 seconds; duty cycle 50-75% fading

  // Setting the seed before using random() the same number of times in the same
  // order ensures each LED gets the same set of 'random' numbers each time.
  random16_set_seed(202);
  unsigned long now = millis();
  for (int i = firstLedAddr; i <= lastLedAddr; i += skip_n_leds + 1) {
    if (i == MODE_LED_LOCATION) continue;  // skip the mode led.
    // 16-bit sine with a period of 1 and operating in milliseconds has a 65535
    // ms period, or ~65.5 seconds. A frequency of 2 --> ~33 seconds, frequency
    // = 32 --> ~2 seconds.
    float frequency = random16(twinkleFrequencyMin, twinkleFrequencyMax) / 1000.0;
    // So they all start off offset from each other.
    int phase_shift = random16();
    // The sine is biased so that it ranges 0-65535 to make duty cycle math more
    // readable.
    uint16_t led_value = sin16((uint16_t)( (now - phase_shift)*frequency )) + 32767;

    // 100 * 1000 = 100% to allow a decent amount of variation coming from the
    // random number generator.
    //
    // Duty cycle must be < 65.5% to fit within a uint16.
    //
    // I found that a duty cycle of 2.5-10% at low
    // frequency leads to a nice calm pace, and keeping the same duty cycle at
    // high frequencies leads to a more frantic mood and plenty of lights
    // flashing on and off. Using random8 is faster, so I scale it up to
    // 2550-10200.
    float duty_cycle = (100000-(random16(twinkleDutyCycleMin, twinkleDutyCycleMax))) / 100000.0;
    uint16_t threshold = (long)(duty_cycle * 65535);

    byte brightness;
    if (led_value < threshold) {
      brightness = 0;
    } else {
      brightness = map(led_value, threshold, 65535, twinkleBrightnessMin, twinkleBrightnessMax);
    }
    hsvs[i].val = brightness;
  }
}

void attackTinesFlash() {
  // Use piecewise cosines to fade in fast and fade out slower.
  unsigned long timeInAttack = millis() - attackStart;
  // Wait until a second into the effect; see attackShaftChase.
  if (timeInAttack < 1000) return;
  timeInAttack -= 1000;
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
    hsvs[i+TINES_START_ADDR].val = brightness;
  }
}

void attackShaftChase() {

  // Need a wave with n nodes traversing the length of the shaft, making
  // physical space (i.e. ringIdx) a factor in the trig function - I believe
  // it'll land in the offset.
  unsigned long timeInAttack = millis() - attackStart;
  for (int ringIdx = 0; ringIdx < NUM_RINGS; ringIdx++) {
    for (int ledIdx = 0; ledIdx < LEDS_PER_RING; ledIdx++) {
      int ledAddr = getLedAddrInRing(ringIdx, ledIdx);
      // Period when frequency=1 is 255 ms. period = 8 --> freq = 1/8 --> ~2 s
      // period. Combine that with a half cosine wave (physicalOffset computation
      // does this, whether the math is right or not...), and we've got one big
      // WA-BAM that hits the top right about 1 second into the effect and dies
      // 1 second later (this couples with attackTinesFlash for timing..)
      uint8_t period = 8;
      // Phase shift the wave as a function of space so it starts at peak
      // brightness at the base of the shaft, minimum brightness at the top, and
      // the wave travels up the shaft. Use a float for floating point math on
      // the brightness.
      float physicalOffset = map(ringIdx, 0, NUM_RINGS-1, 0, 255) * 0.5;
      uint8_t scaled_time = (uint8_t) ((timeInAttack/period) - physicalOffset);
      uint8_t brightness = cos8(scaled_time);

      // Kill the first part of the rising edge to give more 'upward energy' to
      // the chase.
      if (brightness > hsvs[ledAddr].val && brightness < 253) {
        brightness = 0;
      }

      // Do some hoakey math to make sure the chase doesn't start again back at
      // the bottom.
      if (timeInAttack - physicalOffset > 1000 && hsvs[ledAddr].val == 0) {
        continue;
      }

      hsvs[ledAddr].val = brightness;
    }
  }
  return;
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
