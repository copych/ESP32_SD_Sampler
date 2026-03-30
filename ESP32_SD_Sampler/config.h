#pragma once

//******************************************************* DEBUG **********************************************

//#define DEBUG_CORE_TIME
//#define C_MAJOR_ON_START                // play C major chord on startup (testing) and on folder change

//******************************************************* SYSTEM **********************************************
//#define BOARD_HAS_UART_CHIP
#define SAMPLE_RATE           44100       // audio output sampling rate

//#define MIDI_VIA_SERIAL                   // use this option to enable Hairless MIDI on Serial port @115200 baud (USB connector), THIS WILL BLOCK SERIAL DEBUGGING
//#define MIDI_VIA_SERIAL2                  // use this option if you want to operate by standard MIDI @31250baud, UART2 (Serial2), 
#define MIDI_USB_DEVICE                     // use this option if you want to operate via USB with the sampler seen as a MIDI device (consumes RAM)

#define RECEIVE_MIDI_CHAN     0

//******************************************************* FILESYSTEM **********************************************
#define INI_FILE              "sampler.ini"
#define ROOT_FOLDER           "/"         // only </> is supported yet

#ifdef CONFIG_IDF_TARGET_ESP32S3
  #define READ_BUF_SECTORS      5           // that many sectors (assume 512 Bytes) per read operation, the more, the faster it reads
#elif defined CONFIG_IDF_TARGET_ESP32P4
  #define READ_BUF_SECTORS      8           // that many sectors (assume 512 Bytes) per read operation, the more, the faster it reads
#endif

//******************************************************* SAMPLER **********************************************
#ifdef CONFIG_IDF_TARGET_ESP32S3
  #define MAX_POLYPHONY         15          // empiric : MAX_POLYPHONY * READ_BUF_SECTORS <= 156
#elif defined CONFIG_IDF_TARGET_ESP32P4
  #define MAX_POLYPHONY         20          // empiric : MAX_POLYPHONY * READ_BUF_SECTORS <= 180
#endif
#define SACRIFY_VOICES        1           // voices used for smooth transisions to avoid clicks
#define MAX_SAME_NOTES        2           // number of voices allowed playing the same note
#define MAX_VELOCITY_LAYERS   16
#define MAX_NOTES_PER_GROUP   3           // exclusive groups: e.g. Closed hat, Pedal hat and Open hat -- only one of them can play at a time
#define MAX_GROUPS_CROSSES    1           // max possible exclusive groups interleaving (common is 1, meaning no interleavings)
#define MAX_DISTANCE_STRETCH  2           // max distance in semitones to search for an absent sample by changing speed of neighbour files
//#define ADSR_LIVE_UPDATE                  // if you set this param, the notes being played will get the updates along with CC changes (may produce some hisses)

//******************************************************* PINS **********************************************
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #define BUTTON1_GPIO 0
// ESP32 S3
 // #define RGB_LED         38      // RGB LED as a vital sign
  #define MIDIRX_PIN      4       // this pin is used for input when MIDI_VIA_SERIAL2 defined (note that default pin 17 won't work with PSRAM)
  #define MIDITX_PIN      9       // this pin will be used for output (not implemented yet) when MIDI_VIA_SERIAL2 defined
  #define I2S_BCLK_PIN    5       // I2S BIT CLOCK pin (BCL BCK CLK)
  #define I2S_DOUT_PIN    6       // to I2S DATA IN pin (DIN D DAT) 
  #define I2S_WCLK_PIN    7       // I2S WORD CLOCK pin (WCK WCL LCK)

  // ESP32-S3 allows using SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_D0, SDMMC_D1, SDMMC_D2, SDMMC_D3)
  /*
  // default SDMMC GPIOs for S3:
  #define SDMMC_CMD 35
  #define SDMMC_CLK 36
  #define SDMMC_D0  37
  #define SDMMC_D1  38
  #define SDMMC_D2  33
  #define SDMMC_D3  34
  */

  // TYPICAL dev board with two usb type-c connectors doesn't have GPIO 33 and 34
  /*
  #define SDMMC_CMD 3
  #define SDMMC_CLK 46 
  #define SDMMC_D0  9
  #define SDMMC_D1  10
  #define SDMMC_D2  11
  #define SDMMC_D3  12
  */

  // LOLIN S3 PRO for example has a microSD socket, but D1 and D2 are not connected,
  // so if you dare you may solder these ones to GPIOs of your choice (please, refer 
  // to the docs choosing GPIOs, as they may have been used by some device already)
  // https://www.wemos.cc/en/latest/_static/files/sch_s3_pro_v1.0.0.pdf
  // LOLIN S3 PRO version (S3 allows configuring these ones):
  // 
  // DON'T YOU set LOLIN S3 PRO as a target board in Arduino IDE, or you may have problems with MIDI. 
  // SET generic ESP32S3 Dev Module as your target
/*
  #define SDMMC_CMD 11  // LOLIN PCB hardlink
  #define SDMMC_CLK 12  // PCB hardlink
  #define SDMMC_D0  13  // PCB hardlink
  #define SDMMC_D1  18   // my choice //was 8 before
  #define SDMMC_D2  10  // my choice
  #define SDMMC_D3  46  // PCB hardlink
*/

// ESP32S3 allows almost any GPIOs for any particular needs
#define SDMMC_CMD 38
#define SDMMC_CLK 39
#define SDMMC_D0  10
#define SDMMC_D1  11
#define SDMMC_D2  12
#define SDMMC_D3  13


#elif defined(CONFIG_IDF_TARGET_ESP32P4)
  #define BUTTON1_GPIO 8
  #define MIDIRX_PIN      15      // if USE_MIDI_STANDARD is selected as MIDI_IN, this pin receives MIDI messages
  #define MIDITX_PIN      14

  // ===================== I2S PINS ===================================================================================
  #define I2S_BCLK_PIN    48       // I2S BIT CLOCK pin (BCL BCK CLK)
  #define I2S_DOUT_PIN    47       // MCU Data Out: connect to periph. DATA IN (DIN D DAT)
  #define I2S_WCLK_PIN    46       // I2S WORD CLOCK pin (WCK WCL LCK)
  #define I2S_DIN_PIN     -1      // MCU Data In: connect to periph. DATA OUT (DOUT D SD)
  
  // ===================== SD MMC PINS ================================================================================
  // ESP32P4 SPI slot 0 is LOCKED to hardware pins, DON't change it unless you switch to SLOT 1
  // slot 0 defaults:
  #define SDMMC_D0  39
  #define SDMMC_D1  40
  #define SDMMC_D2  41
  #define SDMMC_D3  42
  #define SDMMC_CLK 43
  #define SDMMC_CMD 44
  

#elif defined(CONFIG_IDF_TARGET_ESP32)
// ESP32
  #define MIDIRX_PIN      22      // this pin is used for input when MIDI_VIA_SERIAL2 defined (note that default pin 17 won't work with PSRAM)
  #define MIDITX_PIN      23      // this pin will be used for output (not implemented yet) when MIDI_VIA_SERIAL2 defined
  #define I2S_BCLK_PIN    5       // I2S BIT CLOCK pin (BCL BCK CLK)
  #define I2S_DOUT_PIN    18      // to I2S DATA IN pin (DIN D DAT) 
  #define I2S_WCLK_PIN    19      // I2S WORD CLOCK pin (WCK WCL LCK)  

  // these pins require 10K pull-up, but if GPIO12 is pulled up on boot, we get a bootloop.
  // try using gpio_pullup_en(GPIO_NUM_12) or leave D2 as is 
  // GPIO2 pulled up probably won't let you switch to download mode, so disconnect it 
  // while uploading your sketch

  // General ESP32 (changing these pins is NOT possible)
  #define SDMMC_D0  2
  #define SDMMC_D1  4
  #define SDMMC_D2  12
  #define SDMMC_D3  13
  #define SDMMC_CLK 14
  #define SDMMC_CMD 15
#endif


#if ESP_ARDUINO_VERSION_MAJOR < 3
  #define BYTE_ALIGN 
#else
  #define BYTE_ALIGN 4,
#endif
