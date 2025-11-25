
#include "midi_config.h"

inline void MidiInit() {
  
#ifdef MIDI_VIA_SERIAL 
  #ifdef BOARD_HAS_UART_CHIP
    MIDI_PORT.begin( 115200, SERIAL_8N1 ); // midi port
  #endif
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleControlChange(handleCC);
  MIDI.setHandlePitchBend(handlePitchBend);
  MIDI.setHandleProgramChange(handleProgramChange);
  MIDI.begin(RECEIVE_MIDI_CHAN);
#endif

#ifdef MIDI_VIA_SERIAL2
  pinMode( MIDIRX_PIN , INPUT_PULLDOWN);
  pinMode( MIDITX_PIN , OUTPUT);
  Serial2.begin( 31250, SERIAL_8N1, MIDIRX_PIN, MIDITX_PIN ); // midi port
  MIDI2.setHandleNoteOn(handleNoteOn);
  MIDI2.setHandleNoteOff(handleNoteOff);
  MIDI2.setHandleControlChange(handleCC);
  MIDI2.setHandlePitchBend(handlePitchBend);
  MIDI2.setHandleProgramChange(handleProgramChange);
  MIDI2.begin(RECEIVE_MIDI_CHAN);
#endif

#ifdef MIDI_USB_DEVICE
    // /* Change USB Device Descriptor Parameter
    USB.VID(0x1209);
    USB.PID(0x1304);
    USB.productName("ESP32S3 SD sampler");
    USB.manufacturerName("copych");
    //USB.serialNumber("0000");
    //USB.firmwareVersion(0x0000);
    USB.usbVersion(0x0200);
    USB.usbClass(TUSB_CLASS_AUDIO);
    USB.usbSubClass(0x00);
    USB.usbProtocol(0x00);
    USB.usbAttributes(0x80);
   // */ 
    
    MIDI_usbDev.setHandleNoteOn(handleNoteOn);
    MIDI_usbDev.setHandleNoteOff(handleNoteOff);
    MIDI_usbDev.setHandleControlChange(handleCC);
    MIDI_usbDev.setHandlePitchBend(handlePitchBend);
    MIDI_usbDev.setHandleProgramChange(handleProgramChange);
    MIDI_usbDev.begin(RECEIVE_MIDI_CHAN);
    ESP_LOGI("","USB device started");
#endif
}

inline float midiToExpTime(uint8_t midiCC) { // converts 0-127 range to 0.0 - 1e30 exponential range with slow start and ~infinite growth at 127 (1=0.0002s, 64=2s, 92=10s, 126=80s)
  if (midiCC==0) {return 0.0;}
  if (midiCC==127) {return 1e30;}
  return (float)(0.059970546f * exp(0.058580705f*(float)midiCC - 0.184582266f) - 0.052670842f);
}

inline void handleNoteOn(uint8_t inChannel, uint8_t inNote, uint8_t inVelocity) {    
  /*
  #ifdef RGB_LED
    leds[0].setHue(random(255));
    FastLED.setBrightness(10);
    FastLED.show();
  #endif
  */
  Sampler.noteOn(inNote, inVelocity);
}

inline void handleNoteOff(uint8_t inChannel, uint8_t inNote, uint8_t inVelocity) {  
  /*
  #ifdef RGB_LED
    FastLED.setBrightness(1);
    FastLED.show();
  #endif
  */
  Sampler.noteOff(inNote);
}


void handleCC(uint8_t inChannel, uint8_t cc_number, uint8_t cc_value) {
  bool onoff = 0;
  float scaled;
  switch (cc_number) { // global parameters yet set via ANY channel CCs
    case CC_SUSTAIN:
      onoff = cc_value >> 6;
      Sampler.setSustain(onoff);
      break;
/*
    case CC_RESO:
      scaled = (float)cc_value * MIDI_NORM;
      DJFilter.SetResonance(scaled);
      ESP_LOGI("","Set Resonance %f\r\n", scaled);
      break;
    case CC_CUTOFF:
      scaled = (float)cc_value * MIDI_NORM;
      DJFilter.SetCutoff(scaled);
      ESP_LOGI("","Set Cutoff %f\r\n", scaled);
      break;
    case CC_COMPRESSOR:
      scaled = 3.0f + cc_value * 0.307081f;
      Comp.SetRatio(scaled);
      ESP_LOGI("","Set Comp Ratio %f\r\n", scaled);
      break;
*/
    case CC_ENV_DECAY:
      scaled = midiToExpTime(cc_value);
      Sampler.setDecayTime(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Env Set Decay Time %f s\r\n", scaled);
      break;
    case CC_ENV_ATTACK:
      scaled = midiToExpTime(cc_value);
      Sampler.setAttackTime(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Env Set Attack Time %f s\r\n", scaled);
      break;
    case CC_ENV_RELEASE:
      scaled = midiToExpTime(cc_value);
      Sampler.setReleaseTime(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Env Set Release Time %f s\r\n", scaled);
      break;
    case CC_ENV_SUSTAIN:
      scaled = (float)cc_value * MIDI_NORM;
      Sampler.setSustainLevel(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Env Set Sustain Level %f\r\n", scaled);
      break;
    case CC_REVERB_TIME:
      scaled = (float)cc_value * MIDI_NORM;
      Reverb.SetTime(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Reverb Set Time %f s\r\n", scaled);
      break;
    case CC_REVERB_LVL:
      scaled = (float)cc_value * MIDI_NORM;
      Reverb.SetLevel(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Reverb Set Level %f s\r\n", scaled);
      break;
/*
    case CC_DELAY_TIME:
      scaled = (float)cc_value * MIDI_NORM;
      Delay.SetLength(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Delay Set Time %f s\r\n", scaled);
      break;
    case CC_DELAY_FB:
      scaled = (float)cc_value * MIDI_NORM;
      Delay.SetFeedback(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Delay Set Feedback %f s\r\n", scaled);
      break;
    case CC_DELAY_LVL:
      scaled = (float)cc_value * MIDI_NORM;
      Delay.SetLevel(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Deleay Set Level %f s\r\n", scaled);
      break;
    case CC_DELAY_SEND:
      scaled = (float)cc_value * MIDI_NORM;
      Sampler.setDelaySendLevel(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Set Send To Delay %f s\r\n", scaled);
      break;
*/
    case CC_REVERB_SEND:
      scaled = (float)cc_value * MIDI_NORM;
      Sampler.setReverbSendLevel(scaled);
      ESP_LOGI("","SAMPLER: MIDI: Set Send To Reverb %f s\r\n", scaled);
      break;
    case CC_RESET_CCS:
    case CC_NOTES_OFF:
    case CC_SOUND_OFF:
      //Sampler.endAll();
      break;
  }
}

void handleProgramChange(uint8_t inChannel, uint8_t number) {
}

inline void handlePitchBend(uint8_t inChannel, int number) {
  Sampler.setPitch(number); 
}

inline void c_major() {
  
  #ifdef C_MAJOR_ON_START

  delay(10);
  Sampler.noteOn(60,100);
  Sampler.noteOn(64,100);
  Sampler.noteOn(67,100);
  delay(1000);
  Sampler.noteOff(60, Adsr::END_REGULAR);
  Sampler.noteOff(64, Adsr::END_REGULAR);
  Sampler.noteOff(67, Adsr::END_REGULAR);
  #endif
}