#include "voice.h"
#include "esp_cache.h"

//reading helper
/*
inline IRAM_ATTR __attribute__((always_inline))
int16_t load16(const uint8_t* p) {
    return (int16_t)((p[1] << 8) | p[0]);
}
*/
inline IRAM_ATTR __attribute__((always_inline))
int16_t load16(const uint8_t* p) {
    return *reinterpret_cast<const int16_t*>(p);
}

bool Voice::allocateBuffers() {
  // MALLOC_CAP_INTERNAL
  // MALLOC_CAP_SPIRAM
  #if defined CONFIG_IDF_TARGET_ESP32S3
  uint32_t caps =   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  #elif defined CONFIG_IDF_TARGET_ESP32P4
  uint32_t caps = MALLOC_CAP_DMA;
  #endif
  size_t ram =  heap_caps_get_free_size(caps);
  ESP_LOGI("","VOICE: allocateBuffers: Free RAM: %d ", ram);
  // heap_caps_print_heap_info(caps);
  _buffer0 = (uint8_t*)heap_caps_aligned_alloc( BYTE_ALIGN BUF_SIZE_BYTES + BUF_EXTRA_BYTES , caps);
  _buffer1 = (uint8_t*)heap_caps_aligned_alloc( BYTE_ALIGN BUF_SIZE_BYTES + BUF_EXTRA_BYTES , caps);

  if( _buffer0 == NULL || _buffer1 == NULL){
    ESP_LOGI("","No more RAM for sampler buffer!");
    return false;
  } else {
    ESP_LOGI("","%d Bytes RAM allocated for sampler buffers, &_buffer0=%#010x\r\n", BUF_NUMBER * ( BUF_SIZE_BYTES + BUF_EXTRA_BYTES ) , _buffer0);
  }
  return true;
}


bool Voice::init(SDMMC_FAT32* Card, uint32_t* sustain, uint32_t* normalized){
  _Card = Card;
  _sustain = sustain;
  _normalized = normalized;
  _speedModifier = 1.0f;
  if (!allocateBuffers()) {
    ESP_LOGI("","VOICE: INIT: NOT ENOUGH MEMORY");
    return false;
  }
  AmpEnv.init(SAMPLE_RATE);
  AmpEnv.end(Adsr::END_NOW);
  _active   = false;
  _midiNote = 255;
  _pressed  = false;
  _eof      = true;
  return true;
}


// If the voice is free, it sets the new sample to play
 
void Voice::start(const sample_t smpFile, uint8_t midiNote, uint8_t midiVelo) { // executed in Control Task (Core1)
    _sampleFile             = smpFile;
    _bytesToRead            = smpFile.size;
    _bytesToPlay            = smpFile.byte_offset + smpFile.data_size;
    _amplitude              = 0.0f;
    _bytesPlayed            = 0;
    _coarseBytesPlayed      = 0;
    _fullSampleBytes        = smpFile.channels * smpFile.bit_depth / 8;
    _speed                  = smpFile.speed * _speedModifier;
    _read_buf_sectors       = READ_BUF_SECTORS;
    _bufSizeBytes           = _read_buf_sectors * BYTES_PER_SECTOR;
    _bufSizeSmp             = _bufSizeBytes / _fullSampleBytes;
    _bufEmpty[0]            = true;
    _bufEmpty[1]            = true;
    _bufPosSmp[0]           = _bufSizeSmp;
    _bufPosSmp[1]           = _bufSizeSmp;
    _eof                    = false; 
    // _bufPlayed              = 0;
    _fillBuffer             = _buffer0;
    _playBuffer             = _buffer1;
    _idToFill               = 0;
    _idToPlay               = 1;
    _curChain               = 0;
    _pL1                    = start_byte[_fullSampleBytes / smpFile.channels];
    _pL2                    = _pL1 + _fullSampleBytes;
    _pR1                    = _pL1 + ( _fullSampleBytes / smpFile.channels );
    _pR2                    = _pR1 + _fullSampleBytes;
    _loop                   = (smpFile.loop_mode > 0);
    if (_loop) {
      if (_sampleFile.loop_first_smp >=0 ) {
        _loopFirstSmp = smpFile.loop_first_smp;
      } else {
        _loopFirstSmp = 0;
      }
      if (_sampleFile.loop_last_smp >=0 ) {
        _loopLastSmp = smpFile.loop_last_smp;
      } else {
        _loopLastSmp = _bytesToPlay / _fullSampleBytes - 1;
      }
      _loopFirstSector = (smpFile.byte_offset + _fullSampleBytes * _loopFirstSmp ) / BYTES_PER_SECTOR;
      _loopLastSector  = (smpFile.byte_offset + _fullSampleBytes * _loopLastSmp ) / BYTES_PER_SECTOR;
      
    }
    if (_sampleFile.size == 0) {
      _divFileSize          = 0.001f;
    } else {
      _divFileSize          = 1.0f/((float)smpFile.data_size);
    }
    if (midiVelo > 0) {
      _divVelo = 1.0f/((float)midiVelo);
    } else {
      _divVelo = 256.0f;
    }
    _lastSectorRead         = smpFile.sectors[0].first - 1; 
    _midiNote = midiNote;
    _midiVelo = midiVelo; 
    if (*_normalized) {
      _amp = (float)_midiVelo * MIDI_NORM * 0.000033f;
    } else {
      _amp =   0.000033f;
    }
    _amp *= smpFile.amp;
    _killScoreCoef = (float)_divFileSize * (float)_divVelo;
    
//    _killScoreCoef =  (float)_divFileSize;
    _hungerCoef = (float)_fullSampleBytes * (float)_speed;
 //    ESP_LOGI("","VOICE %d: START note %d velo %d offset %d", my_id, midiNote, midiVelo, smpFile.byte_offset);
    AmpEnv.retrigger(Adsr::END_NOW);
    _active = true;
    _dying = false;
    _pressed = true;
}


void Voice::end(Adsr::eEnd_t end_type){ // most likely being executed in Control Task (Core1)
  switch ((int)end_type) {
    case Adsr::END_NOW:{
      AmpEnv.end(Adsr::END_NOW);
 //     ESP_LOGI("","VOICE %d: END: NOW midi note %d", my_id, _midiNote); 
      _active = false;
      _midiNote = 255;
      _amplitude = 0.0;
      break;
    }
    case Adsr::END_FAST:{
      _dying = true;
      AmpEnv.end(Adsr::END_FAST);
 //     ESP_LOGI("","VOICE %d: END: FAST %d", my_id, _midiNote);
      break;
    }
    case Adsr::END_REGULAR:
    default:{
      if (!_pressed && !(*_sustain)) {
//        ESP_LOGI("","VOICE %d: END: REGULAR %d", my_id, _midiNote); 
        AmpEnv.end(Adsr::END_REGULAR);
      }
    }
  }
}

inline IRAM_ATTR __attribute__((always_inline))
void Voice::getSample(float& sampleL, float& sampleR) {

    sampleL = 0.0f;
    sampleR = 0.0f;

    if (!_active) return;
    if (_bufEmpty[0] & _bufEmpty[1]) return;

    float env = AmpEnv.process() * _amp;

    if (AmpEnv.isIdle()) {
        _active = false;
        _dying = false;
        _midiNote = 255;
        _amplitude = 0.0f;
        return;
    }

    // --- position ---
    const int idx = (int)_bufPosSmpF;
    const float frac = _bufPosSmpF - (float)idx;

    const int bufPosBytes =
        _playBufOffset + idx * _fullSampleBytes;

    const uint8_t* base = &_playBuffer[bufPosBytes];

    // --- load samples (unaligned safe) ---
    const int16_t l1 = load16(base + _pL1);
    const int16_t l2 = load16(base + _pL2);
    const int16_t r1 = load16(base + _pR1);
    const int16_t r2 = load16(base + _pR2);

    // --- interpolate (branchless) ---
    const float dl = (float)(l2 - l1);
    const float dr = (float)(r2 - r1);

    const float outL = (float)l1 + frac * dl;
    const float outR = (float)r1 + frac * dr;

    sampleL = outL * env;
    sampleR = outR * env;

    // --- advance ---
    _bufPosSmpF += _speed;

    // --- update integer view only when needed ---
    _bufPosSmp[_idToPlay] = (int)_bufPosSmpF;

    _bytesPlayed = _coarseBytesPlayed +
        _bufPosSmp[_idToPlay] * _fullSampleBytes;

    // --- end / buffer switch ---
    if (_bytesPlayed >= _bytesToPlay) {
        end(Adsr::END_NOW);
        return;
    }

    if (_bufPosSmp[_idToPlay] > _samplesInPlayBuf) {
        if (_started) toggleBuf();
    }
}

/*
void Voice::getSample(float& sampleL, float& sampleR) {
  float WORD_ALIGNED_ATTR env;
  int WORD_ALIGNED_ATTR bufPosBytes;
  float WORD_ALIGNED_ATTR l1, l2, r1, r2;
  sampleL = 0.0f; 
  sampleR = 0.0f;
  if (!_active ) return;
  if (_bufEmpty[0] && _bufEmpty[1]) {
    return;
  } else {
    env =  (float)AmpEnv.process() * (float)_amp ;
   //  env = _amp;
    if (AmpEnv.isIdle()) {      
      _active = false;
      _dying = false;
      _midiNote = 255;
      _amplitude = 0.0f;
      //  ESP_LOGI("","Voice::getSample: note %d active=false", _midiNote);
      return;
    } else {
      bufPosBytes = (int)_playBufOffset + (int)_fullSampleBytes * (int)_bufPosSmp[_idToPlay ];  // pos in a byte buffer

      //ESP_LOGI("","pos %d \t posF %f", _bufPosSmp[_idToPlay ], _bufPosSmpF);
      l1 = *( reinterpret_cast<volatile int16_t*>( &_playBuffer[ bufPosBytes + _pL1 ] ) );
      l2 = *( reinterpret_cast<volatile int16_t*>( &_playBuffer[ bufPosBytes + _pL2 ] ) );
      sampleL = (float)interpolate( l1, l2, _bufPosSmpF ) * (float)env;
      
      if (_sampleFile.channels == 2){
        r1 = *( reinterpret_cast<volatile int16_t*>( &_playBuffer[ bufPosBytes + _pR1 ] ) );
        r2 = *( reinterpret_cast<volatile int16_t*>( &_playBuffer[ bufPosBytes + _pR2 ] ) );
        sampleR = (float)interpolate( r1, r2, _bufPosSmpF ) * (float)env;
      } else {        
        sampleR = sampleL;
      }
      
      _bufPosSmpF += (float)_speed ; // * _speedModifier;
      _bufPosSmp[_idToPlay ] = _bufPosSmpF;

      _bytesPlayed = _coarseBytesPlayed + (int)_bufPosSmp[_idToPlay] * (int)_fullSampleBytes;
      
    
 //     if ( _bytesPlayed % 16 == 0 ) {
  //      _amplitude = 0.96f * (float)_amplitude + (float)fabs(sampleL) + 0.04f * (float)fabs(sampleR);
 //     } 
     
      if ( _bytesPlayed >= _bytesToPlay ) {
        end(Adsr::END_NOW);
        // ESP_LOGI("","VOICE %d: DATA END: bytes played = %d , bytes to play = %d , pos in buffer = %d ", my_id, _bytesPlayed , _bytesToPlay, _bufPosSmp[_idToPlay]);
      } else { 
        if (_bufPosSmp[_idToPlay ]  > _samplesInPlayBuf ) {
          if (_started) toggleBuf();
        }
      }
    }
  }
}
*/

void  Voice::feed() { // executed in Control Task (Core1)
  if (_bufEmpty[_idToFill] && !_eof) {
    /*
    if (_loop) {
      int bytes_till_loop_end = _loopLastSmp * _fullSampleBytes - _bytesPlayed;
      float fill_coef =  (float)bytes_till_loop_end * (float)DIV_BUF_SIZE_BYTES ;
      if (fill_coef >= 1.0f && fill_coef < 1.42f) { // this situation needs correction
        // change buffer size to allow looping wav data to be loaded from SD (not just a few bytes to play before rewinding to the loop start)
        // divide the remaining bytes appx by half
        
      } 
    }
    */
    int sectorsToRead = _read_buf_sectors;
    int sectorsAvailable;
    volatile uint8_t* bufAddr =  _fillBuffer;
    volatile uint32_t lastSec, firstSec;
    firstSec = lastSec = _lastSectorRead;
    // ESP_LOGI("","VOICE %d: FEED: lastSec before %d", my_id,  lastSec);
    // ESP_LOGI("","fill buf addr %d", bufAddr);
    while (sectorsToRead > 0) {
      sectorsAvailable = min(_sampleFile.sectors[_curChain].last - lastSec, (uint32_t) sectorsToRead) ;
      if (sectorsAvailable > 0) { // we have some sectors in the current chain to read
        // ESP_LOGI("","block available = %d Pointer = %010x", sectorsAvailable, bufAddr);
        _Card->read_block((uint8_t*)bufAddr, lastSec+1, sectorsAvailable);
#ifdef CONFIG_IDF_TARGET_ESP32P4
        esp_cache_msync((void*)bufAddr,
                sectorsAvailable * BYTES_PER_SECTOR,
                ESP_CACHE_MSYNC_FLAG_INVALIDATE);
#endif
        lastSec += sectorsAvailable;
        _bufEmpty[_idToFill]    = false; // bufToFill is now filled with the first sectors of sample file
        sectorsToRead -= sectorsAvailable;
        _bytesToRead -= sectorsAvailable * BYTES_PER_SECTOR;
        bufAddr += sectorsAvailable * BYTES_PER_SECTOR;
      } else { // we've done with the current chain
        if (_curChain + 1 < _sampleFile.sectors.size()) { // we still got some sectors to read
          _curChain++;
          lastSec = _sampleFile.sectors[_curChain].first - 1;
        } else { // this was the last chain of sectors
          _eof = true;
          break;
        }
      }
    }
    // _lastSectorRead could have changed while we were reading here
    //never happened in real life
    //if (firstSec == _lastSectorRead) {
      _lastSectorRead = lastSec;
      // copy first bytes of fillBuffer to playBuffer's extra zone for speeding up interpolation on bufToggle
      memcpy((void*)(_playBuffer + _bufSizeBytes), (const void*)(_fillBuffer ), BUF_EXTRA_BYTES);
      if (!_started) { // init state: bufToFill = 0, bufToPlay = 1
        _idToFill               = 1;
        _idToPlay               = 0;
        _playBuffer             = _buffer0;
        _fillBuffer             = _buffer1;
        _bufPosSmp[_idToFill]   = _bufSizeSmp;
        _bufPosSmp[_idToPlay]   = 0;  
        _bufPosSmpF             = _bufPosSmp[_idToPlay];
        _playBufOffset          = _sampleFile.byte_offset ;
        _samplesInPlayBuf       = (_bufSizeBytes - _playBufOffset) / _fullSampleBytes ;
        memcpy((void*)(_playBuffer + _bufSizeBytes),
        (const void*)(_playBuffer),
        BUF_EXTRA_BYTES);   // self-copy for first pass
        //ESP_LOGI("","VOICE %d: FEED-0: pos: %d, inBuf: %d, offset: %d, BPlyd: %d, firstSec %d, lastSec %d ", my_id, _bufPosSmp[_idToPlay ], _samplesInPlayBuf, _playBufOffset, _bytesPlayed, firstSec, _lastSectorRead );
        _started = true;
      } else {
        _bufPosSmp[_idToFill]   = 0;
        // _fillBufOffset = ( _fullSampleBytes - ( (BUF_SIZE_BYTES - _playBufOffset) % _fullSampleBytes )) % _fullSampleBytes ;
        // _samplesInFillBuf = ((int)BUF_SIZE_BYTES - (int)_fillBufOffset ) / (int)_fullSampleBytes ;
        //ESP_LOGI("","VOICE %d: FEED: pos: %d, inBuf: %d, offset: %d, BPlyd: %d, firstSec %d, lastSec %d ", my_id, _bufPosSmp[_idToPlay ], _samplesInPlayBuf, _playBufOffset, _bytesPlayed, firstSec, _lastSectorRead );
     
      }

    //} else {
    //  ESP_LOGI("","HERE IT IS!!! ");
    //}
  }
}


inline void Voice::toggleBuf() {  // Core0
  if (!_started) return;

  if (_bufEmpty[_idToFill]) {
    // avoid hard discontinuity
    AmpEnv.end(Adsr::END_FAST);
    _dying = true;
    return;
  }

  // --- compute file position ---
  const int playedSamples = (int)_bufPosSmp[_idToPlay];
  const int filePosBytes =
      (int)_coarseBytesPlayed +
      (int)_playBufOffset +
      playedSamples * (int)_fullSampleBytes;

  // --- advance coarse position ---
  _coarseBytesPlayed += _bufSizeBytes;

  // --- preserve fractional position ---
  _bufPosSmpF -= (float)playedSamples;
  _bufPosSmp[_idToFill] = _bufPosSmpF;

  // --- bytes played ---
  _bytesPlayed = filePosBytes - (int)_sampleFile.byte_offset;

  // --- compute new offset ---
  int newOffset = filePosBytes - (int)_coarseBytesPlayed;

  // *** critical: enforce sample alignment ***
  newOffset -= newOffset % (int)_fullSampleBytes;
  _playBufOffset = newOffset;

  _samplesInPlayBuf =
      (_bufSizeBytes - _playBufOffset) / (int)_fullSampleBytes;

  // --- prepare next state (do not publish yet) ---
  uint8_t nextPlay = _idToPlay ^ 1;
  uint8_t nextFill = _idToFill ^ 1;

  uint8_t* nextPlayBuf = (nextPlay == 0) ? _buffer0 : _buffer1;
  uint8_t* nextFillBuf = (nextFill == 0) ? _buffer0 : _buffer1;

  // mark current play buffer empty (safe, no longer used)
  _bufEmpty[_idToPlay] = true;

  // --- memory barrier: ensure all writes above are visible ---
  __sync_synchronize();

  // --- publish new state (coherent switch) ---
  _idToPlay  = nextPlay;
  _idToFill  = nextFill;
  _playBuffer = nextPlayBuf;
  _fillBuffer = nextFillBuf;

  // mark new fill buffer as empty (ready to be filled)
  _bufEmpty[_idToFill] = true;
}




uint32_t Voice::hunger() { // called by SamplerEngine::fillBuffer() in ControlTask, Core1
    if (!_active) return 0;
    if ( _eof) return 0;
    if ( _dying) return 0;
    if (!_bufEmpty[0] && !_bufEmpty[1]) return 0;
    return (/*(float)_speedModifier * */(float)_hungerCoef * ((float)_bufPosSmp[0] + (float)_bufPosSmp[1])); // the bigger the value, the sooner we empty the buffer
}


// linear interpolation of 2 neighbour values by float index (mantissa only used)
inline float Voice::interpolate(float& v1, float& v2, float index) {
  float res;
  int32_t i = (int32_t)index;
  float f = (float)index - i;
  res = (float)f * (float)(v2 - v1) + (float)v1;
  return res;
}


inline float Voice::getKillScore() { // called by SamplerEngine::assignVoice() and freeSomeVoices in ControlTast, Core1
  if (_dying ) return 0.0f; // don't kill twice
  return (float)_bytesPlayed * (float)_killScoreCoef ;
}


inline void Voice::setPitch(float speedModifier) { // called by SamplerEngine::setPitch, Core1
  _speedModifier = speedModifier;
  _speed = _sampleFile.speed * speedModifier;
}
