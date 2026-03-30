 /*
* ESP32-S3 SD Sampler is a polyphonic music synthesizer, which can play PCM WAV samples directly from an SD (microSD) 
* card connected to an ESP32-S3 or ESP32-P4. One directory = one sample set. Plain text "sampler.ini" manages how samples 
* to be spread over the keyboard. The main difference, comparing to the projects available on the net, is that this 
* sampler WON'T try to preload all the stuff into the RAM/PSRAM to play it on demand. So it's not limited in this way 
* by the size of the memory chip and can take really huge (per-note true sampled multi-velocity several gigabytes) 
* sample sets. It only requires that the card is freshly formatted FAT32 and has no or very few bad blocks (actually 
* it requires that the WAV files are written with little or no fragmentation at all). On start it analyzes existing 
* file allocation table (FAT) and forms it's own sample lookup table to be able to access data immediately, using 
* SDMMC with a 4-bit wide bus.
*
*
* Libraries used:
* Fixed string library https://github.com/fatlab101/FixedString
* Arduino MIDI library https://github.com/FortySevenEffects/arduino_midi_library
* If you want to use RGB LEDs, then also FastLED library is needed https://github.com/FastLED/FastLED
*
* (c) Copych 2024, License: MIT https://github.com/copych/ESP32_S3_Sampler?tab=MIT-1-ov-file#readme
* 
* More info:
* https://github.com/copych/ESP32_S3_Sampler
*/

#pragma packed(64)
#pragma GCC optimize ("O2")

#include <Arduino.h>
#include "config.h"
#include "misc.h"
#include "sdmmc.h"
#include <vector>
#include "sampler.h"
#include "fx_reverb.h"
#include <MIDI.h>


#ifdef RGB_LED
  #include "FastLED.h"
  CRGB leds[1];
#endif

//SET_LOOP_TASK_STACK_SIZE(80000);

// =============================================================== MIDI interfaces ===============================================================
#ifdef MIDI_USB_DEVICE
  #include "src/usbmidi/src/USB-MIDI.h"
  USBMIDI_CREATE_INSTANCE(0, MIDI_usbDev);
#endif

#ifdef MIDI_VIA_SERIAL

  struct CustomBaudRateSettings : public MIDI_NAMESPACE::DefaultSettings {
    static const long BaudRate = 115200;
    static const bool Use1ByteParsing = false;
  };

  MIDI_NAMESPACE::SerialMIDI<MIDI_PORT_TYPE, CustomBaudRateSettings> serialMIDI(MIDI_PORT);
  MIDI_NAMESPACE::MidiInterface<MIDI_NAMESPACE::SerialMIDI<MIDI_PORT_TYPE, CustomBaudRateSettings>> MIDI((MIDI_NAMESPACE::SerialMIDI<MIDI_PORT_TYPE, CustomBaudRateSettings>&)serialMIDI);

#endif

#ifdef MIDI_VIA_SERIAL2
// MIDI port on UART2,   pins 16 (RX) and 17 (TX) prohibited on ESP32, as they are used for PSRAM
struct Serial2MIDISettings : public midi::DefaultSettings {
  static const long BaudRate = 31250;
  static const int8_t RxPin  = MIDIRX_PIN;
  static const int8_t TxPin  = MIDITX_PIN;
  static const bool Use1ByteParsing = false;
};
MIDI_NAMESPACE::SerialMIDI<HardwareSerial> Serial2MIDI2(Serial2);
MIDI_NAMESPACE::MidiInterface<MIDI_NAMESPACE::SerialMIDI<HardwareSerial, Serial2MIDISettings>> MIDI2((MIDI_NAMESPACE::SerialMIDI<HardwareSerial, Serial2MIDISettings>&)Serial2MIDI2);
#endif


// =============================================================== MAIN oobjects ===============================================================
SDMMC_FAT32     Card;
SamplerEngine   Sampler;
FxReverb        Reverb;

// =============================================================== GLOBALS ===============================================================
TaskHandle_t SynthTask;
TaskHandle_t ControlTask;
static volatile int DRAM_ATTR WORD_ALIGNED_ATTR out_buf_id = 0;
static volatile int DRAM_ATTR WORD_ALIGNED_ATTR gen_buf_id = 1;
static float DRAM_ATTR WORD_ALIGNED_ATTR sampler_l[DMA_BUF_LEN];     // sampler L buffer
static float DRAM_ATTR WORD_ALIGNED_ATTR sampler_r[DMA_BUF_LEN];     // sampler R buffer
static float DRAM_ATTR WORD_ALIGNED_ATTR mix_buf_l[DMA_BUF_LEN];     // mix L channel
static float DRAM_ATTR WORD_ALIGNED_ATTR mix_buf_r[DMA_BUF_LEN];     // mix R channel
static int16_t DRAM_ATTR WORD_ALIGNED_ATTR out_buf[DMA_BUF_LEN * 2];        // i2s L+R output buffer


// =============================================================== forward declarations ===============================================================
static  void IRAM_ATTR mixer() ;
static  void IRAM_ATTR i2s_output();
static  void IRAM_ATTR sampler_generate_buf();

// =============================================================== PER CORE TASKS ===============================================================
static void IRAM_ATTR audio_task(void *userData) { // core 0 task
  vTaskDelay(10);
  ESP_LOGI("","core 0 audio task run");
  vTaskDelay(20);
  volatile uint32_t WORD_ALIGNED_ATTR t1,t2,t3,t4;
  
  while (true) {
#ifdef DEBUG_CORE_TIME 
    t1=micros();
#endif

    sampler_generate_buf();
    
#ifdef DEBUG_CORE_TIME 
    t2=micros();
#endif

    mixer(); 
    
#ifdef DEBUG_CORE_TIME 
    t3=micros();
#endif

    i2s_output();
    
#ifdef DEBUG_CORE_TIME 
    t4=micros();
    ESP_LOGI("","gen=%d, mix=%d, output=%d, total=%d", t2-t1, t3-t2, t4-t3, t4-t1);
#endif

  }
}
 
static void  IRAM_ATTR control_task(void *userData) { // core 1 task
  byte hue=0;
  vTaskDelay(20);
  ESP_LOGI("","core 1 control task run");
  vTaskDelay(20);
  uint32_t WORD_ALIGNED_ATTR passby = 0;

  while (true) { 

    Sampler.freeSomeVoices();
    
    Sampler.fillBuffer();

    #ifdef MIDI_VIA_SERIAL
      MIDI.read();
    #endif

    #ifdef MIDI_VIA_SERIAL2
      MIDI2.read();
    #endif
    
    #ifdef MIDI_USB_DEVICE
      MIDI_usbDev.read();
    #endif
    
    passby++;

    if (passby%256 == 0 ) { 
    
      processButtons();
      taskYIELD();
      
      #ifdef RGB_LED
      hue++;
      leds[0].setHue(hue); 
      FastLED.show(1);
      #endif
      
    //  ESP_LOGI("","Active voices %d of %d ", Sampler.getActiveVoices(), MAX_POLYPHONY);
    //  ESP_LOGI("","ControlTask unused stack size = %d bytes", uxTaskGetStackHighWaterMark(ControlTask));
    //  ESP_LOGI("","SynthTask unused stack size = %d bytes", uxTaskGetStackHighWaterMark(SynthTask));
    }
  }
}

// =============================================================== SETUP() ===============================================================
void setup() {
  
#ifdef DEBUG_ON
  DEBUG_PORT.begin(115200);
#endif


#ifdef RGB_LED
  FastLED.addLeds<WS2812, RGB_LED, RGB>(leds, 1);
  FastLED.setBrightness(1);
  leds[0].setHue(HUE_RED); //green
  FastLED.show(1);
#endif
delay(2500);

ESP_LOGI("","MIDI: INIT");
  MidiInit();

#ifdef RGB_LED
  leds[0].setHue(HUE_PURPLE); //green
  FastLED.show(1);
#endif



ESP_LOGI("","CARD: BEGIN");
  Card.begin();

//Card.testReadSpeed(READ_BUF_SECTORS,2);

delay(100);

ESP_LOGI("","I2S: INIT");
  i2sInit();

ESP_LOGI("","REVERB: INIT");
  Reverb.Init();
 
  
  Reverb.SetLevel(0.5f);
  Reverb.SetTime(0.7f);
  Sampler.setReverbSendLevel(0.5f);


//heap_caps_print_heap_info(MALLOC_CAP_8BIT || MALLOC_CAP_INTERNAL);
ESP_LOGI("","SAMPLER: INIT");
  Sampler.init(&Card);
  
  Sampler.setCurrentFolder(1);

  initButtons();
  
  xTaskCreatePinnedToCore( audio_task, "SynthTask", 4000, NULL, 8, &SynthTask, 0 );
  xTaskCreatePinnedToCore( control_task, "ControlTask", 9000, NULL, 5, &ControlTask, 1 );
 
  c_major();

  vTaskDelay(100);

  ESP_LOGI("","Setup() DONE");
 // heap_caps_print_heap_info(MALLOC_CAP_8BIT );
 
#ifdef RGB_LED
  leds[0].setHue(HUE_GREEN); //green
  FastLED.show(1);
#endif
}


void loop() {
  char res[800];
  
  vTaskDelay(25);
  vTaskList(res) ;
  ESP_LOGI("", "%s\n\n", res);

  heap_caps_print_heap_info( MALLOC_CAP_INTERNAL );
            

  vTaskDelay(30);

  ESP_LOGI("","LOOP: killing task");
  taskYIELD();


  vTaskDelete(NULL);
}
