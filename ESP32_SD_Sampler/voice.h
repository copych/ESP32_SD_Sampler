#pragma once
#define   BUF_EXTRA_BYTES   32    // for buffer overlapping
#define   BUF_NUMBER        2     // tic-tac don't change, for readability only
#define   CHANNELS          2     // 1 = mono, 2 = stereo
#define   BYTES_PER_CHANNEL 2

const int   BUF_SIZE_BYTES      = (READ_BUF_SECTORS * BYTES_PER_SECTOR);
const float DIV_BUF_SIZE_BYTES  = (1.0f / BUF_SIZE_BYTES);
const int   start_byte[5]       = { 0, 0, 0, 1, 2 }; // offset values for [-], 8, 16, 24, 32 pcm bits per channel 

#include "adsr.h"
#include "sdmmc.h"

typedef struct __attribute__((packed)){
  char riff[4] = {'R', 'I', 'F', 'F'};
  uint32_t fileSize;                    // actually, ( filesize-8 )
  char waveType[4] = {'W', 'A', 'V', 'E'};
  char format[4] = {'f', 'm', 't', ' '};
  uint32_t lengthOfData = 16;           // length of the rest of the header : 16 for PCM
  uint16_t audioFormat = 1;             // 1 for PCM
  uint16_t numberOfChannels = 2;        // 1 - mono, 2 - stereo
  uint32_t sampleRate = 44100;        
  uint32_t byteRate = 44100 * 2 * 2;    // sample rate * number of channels * bytes per sample
  uint16_t blockAlign = 16 / 8  * 2;    // bytes per sample (all channels)
  uint16_t bitsPerSample = 16;
  char dataStr[4] = {'d', 'a', 't', 'a'};
  uint32_t dataSize;  
} wav_header_t;

typedef struct {
  int       byte_offset = 44;     // = wav header size
  uint32_t  size;
  uint32_t  data_size;
  float     orig_freq;
  float     speed         = 0.0f; // samples
  uint8_t   orig_velo_layer;
  int       sample_rate   = 44100;
  int       channels      = -1;
  int       bit_depth     = -1;
  float     amp           = 1.0f;
//  float     attack_time   = 0.0f;
//  float     decay_time    = 0.5f;
//  float     sustain_level = 1.0f;
//  float     release_time  = 12.0f;
  int       loop_mode     = 0; // 0 = none,  1 = forward
  int32_t   loop_first_smp= -1;
  int32_t   loop_last_smp = -1;
  bool      native_freq   = false;
  // FixedString<4>    name; // only used in SamplerEngine::printMapping()
  std::vector<chain_t>   sectors;
} sample_t;

class Voice {
  public:
    Voice(){};
    bool              init(SDMMC_FAT32* Card, uint32_t* sustain, uint32_t* normalized);
    bool              allocateBuffers();
    void              getSample(float& L, float& R);
    inline float      interpolate(float& s1, float& s2, float i);
    void              start(const sample_t nextSmp, uint8_t nextNote, uint8_t nextVelo);
    void              end(Adsr::eEnd_t);
    void              fadeOut();
    void              feed();
    inline uint32_t   hunger();
    inline void       setStarted(uint32_t st)   {_started = st;}
    inline void       setPressed(uint32_t pr)   {_pressed = pr;}
    inline void       setPitch(float speedModifier);
    inline int        getChannels()   {return _sampleFile.channels;}
    inline uint32_t       isActive()      {return _active;}
    inline uint32_t       isDying()       {return _dying;}
    inline uint8_t    getMidiNote()   {return _midiNote;}
    inline uint8_t    getMidiVelo()   {return _midiVelo;}
    inline uint32_t   getBufPlayed()  {return _bufPlayed;}
    inline float      getAmplitude()  {return _amplitude;}
    inline float      getKillScore()        ;
    inline int        getPlayPos()    {return _bufPosSmp[_idToPlay];}
    inline uint32_t   getBufSize()    {return _bufSizeSmp;}
    inline void       toggleBuf();
    inline void       setAttackTime(float timeInS)    {AmpEnv.setAttackTime(timeInS, 0.0f);}
    inline void       setDecayTime(float timeInS)     {AmpEnv.setDecayTime(timeInS);}
    inline void       setReleaseTime(float timeInS)   {AmpEnv.setReleaseTime(timeInS);}
    inline void       setSustainLevel(float normLevel){AmpEnv.setSustainLevel(normLevel);}
    int my_id =0;
    
  private:
  // some members are volatile because they are used in different tasks on both cores, while real-time conditions require immediate changes without caching 
    SDMMC_FAT32*        _Card                   ;
    uint32_t*               _sustain                ; // every voice needs to know if sustain is ON. 
    uint32_t*               _normalized             ;
    float WORD_ALIGNED_ATTR              _amp                    = 1.0f;    
    uint32_t                _active                 = false;
    volatile uint32_t       _dying                  = false;
    volatile uint32_t       _started                = false;
    uint8_t* WORD_ALIGNED_ATTR           _buffer0;                         // pointer to the 1st allocated SD-reader buffer
    uint8_t* WORD_ALIGNED_ATTR           _buffer1;                         // pointer to the 2nd allocated SD-reader buffer
    uint8_t* WORD_ALIGNED_ATTR           _playBuffer;                      // pointer to the buffer which is being played (one of the two toggling buffers)
    uint8_t* WORD_ALIGNED_ATTR           _fillBuffer;                      // pointer to the buffer which awaits filling (one of the two toggling buffers)
    uint8_t             _buffersReady           = 0;  // counter
    uint32_t            _bufSizeBytes           = BUF_SIZE_BYTES;
    uint32_t            _read_buf_sectors       = READ_BUF_SECTORS;
    uint32_t            _bufSizeSmp             = 0;
    int                 _changedBufBytes        = 0;
    uint32_t            _hunger                 = 0;
    int                 _bytesToRead            = 0;      // can be negative
    uint32_t            _bytesToPlay            = 0;
    int                 _playBufOffset          = 0;      // play-buffer byte offset till the 1st sample
    int                 _fillBufOffset          = 0;      // fill-buffer byte offset till the 1st sample
    int WORD_ALIGNED_ATTR       _pL1, _pL2, _pR1, _pR2  ;
    int                 _samplesInFillBuf       = 0;
    int                 _samplesInPlayBuf       = 0;
    volatile int WORD_ALIGNED_ATTR       _posSmp                 = 0;      // global position in terms of samples
    volatile int WORD_ALIGNED_ATTR       _bufPosSmp[2]           = {0, 0}; // sample pos, it depends on the number of channels and bit depth of a wav file assigned to this voice;
    float WORD_ALIGNED_ATTR              _bufPosSmpF             = 0.0f;   // exact calculated sample reading position including speed, pitchbend etc. 
    bool                _bufEmpty[2]            = {true, true};
    uint32_t WORD_ALIGNED_ATTR           _fullSampleBytes        = 4;      // bytes
    float               _divFileSize            = 0.001f;
    float               _divVelo                = 0;
    volatile int        _idToFill               = 0;      // tic-tac buffer id
    volatile int        _idToPlay               = 1;
    uint8_t             _midiNote               = 0;
    uint8_t             _midiVelo               = 0;    
    float               _speed                  = 1.0f;   // _speed param corrects the central freq of a sample 
    float               _speedModifier          = 1.0f;   // pitchbend, portamento etc. 
    volatile uint32_t   _lastSectorRead         = 0;      // last sector that was read during this sample playback
    uint32_t            _curChain               = 0;      // current chain (linear non-fragmented segment of a sample file) index
    uint32_t            _bufPlayed              = 0;      // number of buffers played (for float correction)
    uint32_t            _coarseBytesPlayed      = 0;
    uint32_t            _bytesPlayed            = 0;
    float               _amplitude              = 0.0f;
    volatile uint32_t       _pressed                = false;
    volatile uint32_t       _eof                    = true;
    volatile float      _killScoreCoef          = 1.0f;
    volatile float      _hungerCoef             = 1.0f;
    uint32_t                _loop                   = false;
    int                 _loopState              = 0;
    uint32_t            _loopFirstSmp           = 0;
    uint32_t            _loopLastSmp            = 0;
    uint32_t            _loopFirstSector        = 0;
    uint32_t            _loopLastSector         = 0;
    int                 _lowest                 = 1;
    Adsr                AmpEnv                  ;
    sample_t WORD_ALIGNED_ATTR            _sampleFile             ;
};
