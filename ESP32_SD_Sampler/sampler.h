#pragma once

#define WAV_CHANNELS          2           // do not change, not implemented
#define MAX_CONFIG_LINE_LEN   256         // 4 < x < 256 , must be divisible by 4, no need to change this
#define STR_LEN               MAX_CONFIG_LINE_LEN

#include <vector>
#include <FixedString.h>
#include "voice.h"
#include "sdmmc.h"

enum eVoiceAlloc_t  { VA_OLDEST, VA_MOST_QUIET, VA_PERCEPTUAL, VA_NUMBER }; // not implemented
enum eVeloCurve_t   { VC_LINEAR, VC_CUSTOM, VC_SOFT1, VC_SOFT2, VC_SOFT3, VC_HARD1, VC_HARD2, VC_HARD3, VC_CONST, VC_NUMBER }; // VC_LINEAR, VC_CUSTOM implemented
enum eItem_t        { P_NUMBER, P_NAME, P_MIDINOTE, P_OCTAVE, P_SEPARATOR, P_VELO, P_INSTRUMENT }; // filename template elements 
enum eInstr_t       { SMP_MELODIC, SMP_PERCUSSIVE }; 
enum eSection_t     { S_NONE, S_SAMPLESET, S_FILENAME, S_NOTE, S_RANGE, S_GROUP };

using str8_t    = FixedString<8>; 
using str20_t   = FixedString<20>;
using str64_t   = FixedString<64>;
using str256_t  = FixedString<STR_LEN>;
using variants_t = std::vector<str20_t>;

typedef struct {
  uint8_t     first  = 0     ;       // lowest midi note in range
  uint8_t     last   = 127     ;       // highest midi note in range
  str20_t     instr       = "";
  bool        noteoff     = true;
  float       speed       = 1.0f;
  int         velo_layer  = -1;
  int         limit_same  = 2;
  float       attack_time   = 0.0f;
  float       decay_time    = 0.5f;
  float       sustain_level = 1.0f;
  float       release_time  = 12.0f;
  bool        loop          = false;
  inline void clear(eInstr_t t) {
    first         = 0;
    last          = 127;
    instr         = "";
    velo_layer    = -1;
    noteoff       = (t != SMP_PERCUSSIVE) ;
    speed         = 1.0f;
    limit_same    = 2;
    attack_time   = 0.0f;
    decay_time    = 0.5f;
    sustain_level = 1.0f;
    release_time  = 12.0f;
    loop          = false;
  }
} ini_range_t;

typedef struct {
  str8_t      name[2]       ;       // sharp (#) and flat (b) variants
  int         octave        ;       // octave number (-1 to 9)
  float       freq          ;       // calculated frequency for each midi note (A4=440Hz)
  float       tuning        = 1.0f; // 0.9 = 10% lower, 1.1 = 10% higher
  int         transpose     = 0;    // +/- semitones
  bool        noteoff       = true; // should it handle note off messages
  int         group         = -1;
  int         limit_same    = 2;
  float       attack_time   = 0.0f;
  float       decay_time    = 0.01f;
  float       sustain_level = 1.0f;
  float       release_time  = 0.05f;
  bool        loop          = false;
} midikey_t;

typedef struct {
  eItem_t     item_type;
  str20_t     item_str;
} template_item_t;


const str8_t notes[2][12]= {
  {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},
  {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"}
}; 
  
class SamplerEngine {
  
  public:
    SamplerEngine() {}; 
    void            init(SDMMC_FAT32* Card);
    void            initKeyboard();
    void            fadeOut(int id);
    void            getSample(float& sampleL, float& sampleR);
    fname_t         getFolderName(int id)                 { return _folders[id]; }
    fname_t         getCurrentFolder()                    { return _currentFolder; }
    int             getActiveVoices();
    void            freeSomeVoices();
    int             scanRootFolder();                       // scans root folder for sample directories, returns count of valid sample folders, -1 if error
    void            loadInstrument(uint8_t noteLow=0, uint8_t noteHigh=127); // loads config and prepares samples from the current folder
    inline void     setSampleRate(unsigned int sr)        { if (sr > 1) _sampleRate = sr; }
    inline void     setRootFolder(const fname_t& rf)      { _rootFolder = rf; }
    inline void     setMaxVoices(byte mv)                 { _maxVoices = constrain(mv, 1, MAX_POLYPHONY); }
    inline void     setVoiceAllocMethod(eVoiceAlloc_t va) { _voiceAllocMethod = va ; }
    inline void     setCurrentFolder(int folder_id);        // sets current folder to the desired path[folder_id]
    inline void     setNextFolder();                        // sets current folder to the next dir which was found during scanFolders()
    inline void     setPrevFolder();                        // sets current folder to the previous dir which was found during scanFolders()
    inline void     setSustain(bool onoff)                ;
    inline void     setPitch(int num)                     ; // -8192 .. 8191
    inline void     setDelaySendLevel(float val)          {_sendDelay = val;}
    inline void     setReverbSendLevel(float val)         {_sendReverb = val;}
    inline void     setVolume(float val)                  {_amp = val;}
    inline void     setPano(float val)                    {_pano = val;}
    inline float    getDelaySendLevel()                   {return _sendDelay;}
    inline float    getReverbSendLevel()                  {return _sendReverb;}
    inline float    getVolume()                           {return _amp;}
    inline float    getPano()                             {return _pano;}
    
    void            storeGroup( variants_t& vars );
    void            setSustainLevel(float seconds);
    void            setAttackTime(float seconds);
    void            setDecayTime(float seconds);
    void            setReleaseTime(float seconds);
    inline void     noteOn(uint8_t midiNote, uint8_t velocity);
    inline void     noteOff(uint8_t midiNote, Adsr::eEnd_t end_type = Adsr::END_REGULAR);
    void            fillBuffer();
    
  private:
    SDMMC_FAT32*    _Card;
    inline int      assignVoice(byte midi_note, byte midi_velocity);    // returns id of a slot to use for a new note
    void            parseIni();                  // loads config from current folder, determining how wav files spread over the notes/velocities
    bool            parseFilenameTemplate(str256_t& line);
    void            processNameParser(entry_t* entry);
    eSection_t      parseSection( str256_t& val );
    bool            parseBoolValue( str256_t& val );
    float           parseFloatValue( str256_t& val );
    int             parseIntValue( str256_t& val );
    eInstr_t        parseInstrType( str256_t& val );
    variants_t      parseVariants( str256_t& val); 
    void            parseLimits( str256_t& val); 
    void            parseWavHeader(entry_t* entry, sample_t& smp);
    void            applyRange(ini_range_t& range);
    void            finalizeMapping();
    void            buildVeloCurve();
    uint8_t         mapVelo(uint8_t velo);
    uint8_t         unMapVelo(uint8_t mappedVelo);
    void            resetSamples();
    uint8_t         midiNoteByName(str8_t noteName);
    void            printMapping();
    sample_t WORD_ALIGNED_ATTR       _sampleMap[128][MAX_VELOCITY_LAYERS];
    midikey_t       _keyboard[128];
    uint8_t         _groups[128][ ( ( MAX_NOTES_PER_GROUP - 1 ) * MAX_GROUPS_CROSSES ) ];      // each of 128 elements contains notes to shoot when it starts
    float           _ampCurve[128];       // velocity to amplification mapping [0.0 ... 1.0] to make seamless velocity response curve
    eVeloCurve_t    _veloCurve            = VC_LINEAR;
    uint8_t         _veloMap[128];
    uint8_t         _veloLayers           = 1;
    float           _divVeloLayers        = 1.0f;
    int             _sampleRate           = SAMPLE_RATE;
    uint8_t         _sampleChannels       = WAV_CHANNELS; // 2 = stereo, 1 = mono : use or not stereo data in sample files
    uint8_t         _maxVoices            = MAX_POLYPHONY; 
    int             _limitSameNotes       = MAX_SAME_NOTES;
    fname_t         _rootFolder           ;
    volatile int    _currentFolderId      = 0;
    fname_t         _currentFolder        ;
    int             _sampleSetsCount      = 0;    
    float           _sendDelay            = 0.3f;
    float           _sendReverb           = 0.3f;
    float           _amp                  = 0.9f;
    float           _pano                 = 0.5f;
    float           _attackTime           = 0.0f;
    float           _decayTime            = 0.05f;
    float           _releaseTime          = 8.0f;
    float           _sustainLevel         = 1.0f;
    float           _speed                = 1.0f;
    int             _pitchBendSemitones   = 2;
    eVoiceAlloc_t   _voiceAllocMethod     = VA_OLDEST; // not implemented, VA_PERCEPTUAL is gonna be the only one
    int             _parser_i             = 0;
    uint32_t            _normalized           = false;
    uint32_t            _sustain              = false;
    eInstr_t        _type                 = SMP_MELODIC;
    str64_t         _title                = "";
    Voice WORD_ALIGNED_ATTR          Voices[MAX_POLYPHONY]  ;
    variants_t      _veloVars              ;
    std::vector<fname_t>          _folders ;
    std::vector<template_item_t>  _template;
    std::vector<ini_range_t>      _ranges  ;
};
