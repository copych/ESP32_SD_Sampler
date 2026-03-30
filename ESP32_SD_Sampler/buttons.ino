
#define MUXED_BUTTONS 0
#define GPIO_BUTTONS 1
#define TOTAL_BUTTONS 1
#define LOGICAL_ON LOW
const int buttonGPIOs[]{ BUTTON1_GPIO };

bool autoFireEnabled = false;                 // should the buttons generate continious clicks when pressed longer than a longPressThreshold
bool lateClickEnabled = false;                // enable registering click after a longPress call
const unsigned long longPressThreshold = 800; // the threshold (in milliseconds) before a long press is detected
const unsigned long autoFireDelay = 500;      // the threshold (in milliseconds) between clicks if autofire is enabled
const unsigned long riseThreshold = 20;       // the threshold (in milliseconds) for a button press to be confirmed (i.e. debounce, not "noise")
const unsigned long fallThreshold = 10;       // debounce, not "noise", also this is the time, while new "touches" won't be registered

void readButtonsState(uint32_t &activeFlags) {
  activeFlags=0;
#if MUXED_BUTTONS > 0
  // assuming that first 16 buttons are connected via 16 ch multiplexer
  for (int i = 0; i < MUXED_BUTTONS; i++) {
    // if (readMuxed(i)) {bitSet(activeFlags, i);} else {bitClear(activeFlags, i);}
    bitWrite(activeFlags, i, readMuxed(i));
  }
#endif
  // all the rest are just GPIO buttons 
  for (int i = 0; i < GPIO_BUTTONS; i++) {
    bitWrite(activeFlags, i + MUXED_BUTTONS, digitalRead(buttonGPIOs[i]) == LOGICAL_ON);
  }
}

void initButtons() {
  for (int i = 0; i < GPIO_BUTTONS; i++) {
#if LOGICAL_ON == HIGH
    pinMode(buttonGPIOs[i], INPUT_PULLDOWN);
#else
    pinMode(buttonGPIOs[i], INPUT_PULLUP);
#endif
  }
}

#if MUXED_BUTTONS>0
bool readMuxed(int channel) { 
  // select the multiplexer channel
  digitalWrite(MUX_S0, bitRead(channel, 0));
  digitalWrite(MUX_S1, bitRead(channel, 1));
  digitalWrite(MUX_S2, bitRead(channel, 2));
  digitalWrite(MUX_S3, bitRead(channel, 3));
  delayMicroseconds(1);
  // read channel: LOGICAL_ON should be defined as HIGH or LOW depending on hardware settings (PullHigh or PullLow)
  return (digitalRead(MUX_Z) == LOGICAL_ON); 
}
#endif

#ifdef ENC_CLK
void processEncoder() { // idea was taken from Alex Gyver's examples
  static int newState = 0;
  static int oldState = 0;
 // static const int8_t stepIncrement[16] = {0, 1, -1, 0,  -1, 0, 0, 1,  1, 0, 0, -1,  0, -1, 1, 0}; // Half-step encoder
  static const int stepIncrement[16] = {0, 0, 0, 0,  -1, 0, 0, 1,  1, 0, 0, -1,  0, 0, 0, 0}; // Full-step encoder

  unsigned int clk = digitalRead(ENC_CLK)==LOGICAL_ON;
  unsigned int dt = digitalRead(ENC_DT)==LOGICAL_ON;
  ESP_LOGI("",clk);
  ESP_LOGI("","\t");
  ESP_LOGI("",dt);
  newState = (clk | dt << 1);
  if (newState != oldState) {
    int stateMux = newState | (oldState << 2);
    int rotation = stepIncrement[stateMux];
    oldState = newState;
    if (rotation != 0) {
      encoderMove(rotation);
    }
  }
}
#endif

void processButtons() {
  static uint32_t buttonsState = 0;                           // current readings
  static uint32_t oldButtonsState = 0;                        // previous readings
  static uint32_t activeButtons = 0;                          // activity bitmask
  static unsigned long currentMillis;                         // not to call repeatedly within loop
  static unsigned long riseTimer[TOTAL_BUTTONS];              // stores the time that the button was pressed (relative to boot time)
  static unsigned long fallTimer[TOTAL_BUTTONS];              // stores the duration (in milliseconds) that the button was pressed/held down for
  static unsigned long longPressTimer[TOTAL_BUTTONS];              // stores the duration (in milliseconds) that the button was pressed/held down for
  static unsigned long autoFireTimer[TOTAL_BUTTONS];          // milliseconds before firing autoclick
  static bool buttonActive[TOTAL_BUTTONS];                    // it's true since first touch till the click is confirmed
  static bool pressActive[TOTAL_BUTTONS];                     // indicates if the button has been pressed and debounced
  static bool longPressActive[TOTAL_BUTTONS];                 // indicates if the button has been long-pressed
  
  readButtonsState(buttonsState);                    // call a reading function
  
  for (int i = 0 ; i < TOTAL_BUTTONS; i++) {
    if ( bitRead(buttonsState, i) != bitRead(oldButtonsState, i) ) {
      // it changed
      if ( bitRead(buttonsState, i) == 1 ) {                  // rise (the beginning)
        // launch rise timer to debounce
        if (buttonActive[i]) {                                // it's active, but rises again
          // it might be noise, we check the state and timers
          if (pressActive[i]) {
            // it has passed the rise check, so it may be the ending phase
            // or it may be some noise during pressing
          } else {
            // we are still checking the rise for purity
            if (millis() - riseTimer[i] > riseThreshold) {
              // not a good place to confirm but we have to
              pressActive[i] = true;
            }
          }
        } else {
          // it wasn't active lately, now it's time
          buttonActive[i] = true; // fun begins for this button
          riseTimer[i] = millis();
          longPressTimer[i] = millis(); // workaround for random longPresses
          autoFireTimer[i] = millis(); // workaround 
          bitWrite(activeButtons, i, 1);
          onTouch(i, activeButtons); // I am the first          
        }
      } else {                                                // fall (is it a click or just an end?)
        // launch fall timer to debounce
        fallTimer[i] = millis();
       // pendingClick[i] = true;
      }
    } else {                                                  // no change for this button
      if ( bitRead(buttonsState, i) == 1 ) {                  // the button reading is "active"
        // someone's pushing our button
        if (!pressActive[i] && (millis() - riseTimer[i] > riseThreshold)) {
          pressActive[i] = true;
          longPressTimer[i] = millis();
          onPress(i, activeButtons);
        }
        if (pressActive[i] && !longPressActive[i] && (millis() - longPressTimer[i] > longPressThreshold)) {
          longPressActive[i] = true;
          onLongPress(i, activeButtons);
        }
        if (autoFireEnabled && longPressActive[i]) {
          if (millis() - autoFireDelay > autoFireTimer[i]) { 
            autoFireTimer[i] = millis();
            onAutoClick(i, activeButtons);                    // function to execute on autoClick event 
          }
        }
      } else {                                                // the button reading is "inactive"
        // ouch! a click?
        if (buttonActive[i] && (millis() - fallTimer[i] > fallThreshold)) {
          // yes, this is a click
          buttonActive[i] = false; // bye-bye
          pressActive[i] = false;
          if (!longPressActive[i] || lateClickEnabled) {
            onClick(i, activeButtons);
          }
          longPressActive[i] = false;
          onRelease(i, activeButtons);
        }
      }
    }
    bitWrite(activeButtons, i, buttonActive[i]);
  }
  oldButtonsState = buttonsState;
}


// buttonNumber - variable that indicates the button caused the event
// activeButtonsBitmask flags all the active buttons 
// each binary digit contains a state: 0 = inactive, 1 = active
void onTouch (uint8_t buttonNumber, uint32_t activeButtonsBitmask) {
  ESP_LOGI("","Flags: %d Touch: %d",activeButtonsBitmask , buttonNumber);

}
  
void onPress (uint8_t buttonNumber, uint32_t activeButtonsBitmask) {

  ESP_LOGI("","Flags: %d Press: %d",activeButtonsBitmask , buttonNumber);
  Sampler.setNextFolder();
  c_major();
}
  
void onClick (uint8_t buttonNumber, uint32_t activeButtonsBitmask) {
  
  ESP_LOGI("","Flags: %d Click: %d",activeButtonsBitmask , buttonNumber);
}
  
void onLongPress (uint8_t buttonNumber, uint32_t activeButtonsBitmask) {
  
  ESP_LOGI("","Flags: %d LongPress: %d",activeButtonsBitmask , buttonNumber);
}
  
void onAutoClick (uint8_t buttonNumber, uint32_t activeButtonsBitmask) {
  
  ESP_LOGI("","Flags: %d AutoClick: %d",activeButtonsBitmask , buttonNumber);
}

void onRelease (uint8_t buttonNumber, uint32_t activeButtonsBitmask) {
  
  ESP_LOGI("","Flags: %d Release: %d",activeButtonsBitmask , buttonNumber);
 
}
#ifdef ENC_CLK
void encoderMove (int8_t rotation) {
  ESP_LOGI("","Encoder: %d", rotation);
  // Check Rotary-Encoder-Movements
 
}
#endif

 
  
