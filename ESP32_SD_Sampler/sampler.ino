#include "sampler.h"
#include "sdmmc_file.h"

void SamplerEngine::init(SDMMC_FAT32* Card){
  _Card = Card;
  bool good=true;
  _rootFolder = ROOT_FOLDER;
  _limitSameNotes = MAX_SAME_NOTES;
  _maxVoices = MAX_POLYPHONY;
  int num_sets = scanRootFolder();
  for (int i = 0; i < num_sets; i++) {
    ESP_LOGI("","Folder %d : %s" , i ,_folders[i].c_str());
  }
  ESP_LOGI("","Total %d folders with samples found\r\n", num_sets);
  initKeyboard();
  for (int i = 0 ; i < MAX_POLYPHONY ; i++) {
    ESP_LOGI("","Voice %d: ", i);
    // sustain is global and needed for every voice, so we just pass a pointer to it.
    if (!Voices[i].init(Card, &_sustain, &_normalized)) {
      _maxVoices = i;
      break;
    }
    Voices[i].my_id = i;
    _allocatedVoices = i + 1;
  }
  _maxVoices = _allocatedVoices;
  _adaptivePolyphony.reset(_allocatedVoices, getUnderruns(), _cpuPressure.load(), millis());
  if (!_allocatedVoices) {
    ESP_LOGE("SAMPLER", "No audio buffers available");
    while (true) delay(1000);
  }
  if (num_sets > 0) {
    setSampleRate(SAMPLE_RATE);
    ESP_LOGI("","+++");
  } else {
    while(true) {
      // loop forever
      delay(1000);
      ESP_LOGI("","--- no samples found");
    }      
  }
  setReverbSendLevel(0.2f);
  setDelaySendLevel(0.0f);
}

int SamplerEngine::scanRootFolder() {  
  fpath_t dirname;
  ESP_LOGI("","\r\nSAMPLER: Scanning root folder");
  SDMMC_FileReader Reader(_Card);
  _rootFolder = ROOT_FOLDER;
  _folders.clear();
  _Card->setCurrentDir(_rootFolder);
  int index = 0;
  while (true) {
    entry_t* entry = _Card->nextEntry();
    if (entry->is_end) break;
    if (entry->is_dir) {
      point_t p = _Card->getCurrentPoint();
      dirname = entry->name;      
      ESP_LOGI("", " dirname: %s" , dirname.c_str());
      _Card->setCurrentDir(dirname);
      if (Reader.open(INI_FILE) == ESP_OK ) {
        Reader.close();
        _folders.push_back(dirname);
        index++;
      }
      _Card->setCurrentPoint(p);
    }
  }
  _sampleSetsCount = index;
  return index;
}

inline int SamplerEngine::assignVoice(byte midi_note, byte velo){
  float maxVictimScore = 0.0;
  float minAmp = 1.0e30;
  int id = 0;
  for (int i = 0 ; i < _maxVoices ; i++) {
    if (!Voices[i].isActive()){
   //   ESP_LOGI("","SAMPLER: First vacant voice");
      return (int)i;
    }
  }  

  for (int i = 0 ; i < _maxVoices ; i++) {
    if (Voices[i].getKillScore() > maxVictimScore){
      maxVictimScore = Voices[i].getKillScore();
      id = i;
    }
  }
  return id;
}

inline void SamplerEngine::noteOn(uint8_t midiNote, uint8_t velo){
  if (midiNote > 127) return;
  if (!velo) { noteOff(midiNote); return; }
  int i = assignVoice(midiNote, velo);
  const sample_t& smp = _sampleMap[midiNote][mapVelo(velo)];
  for (int n = 0; n < (( MAX_NOTES_PER_GROUP - 1 ) * MAX_GROUPS_CROSSES ); n++ ) {
    if (_groups[midiNote][n] == 255) break;    // terminate
    ESP_LOGI("","SAMPLER: GROUP KILL: %d", _groups[midiNote][n]);
    noteOff(_groups[midiNote][n], Adsr::END_SEMI_FAST);      // provide exclusivity
  }
  if (smp.source != INVALID_SAMPLE_SOURCE && smp.source < _sampleSources.size()) {
    if (Voices[i].isActive()) ++_stealsImmediate;
   // ESP_LOGI("","SAMPLER: voice %d note %d velo %d", i, midiNote, velo);
    Voices[i].end(Adsr::END_NOW);
    Voices[i].setAttackTime(_keyboard[midiNote].attack_time);
    Voices[i].setDecayTime(_keyboard[midiNote].decay_time);
    Voices[i].setReleaseTime(_keyboard[midiNote].release_time);
    Voices[i].setSustainLevel(_keyboard[midiNote].sustain_level);
    Voices[i].start(_sampleSources[smp.source], smp, midiNote, velo);
  } else {
    ESP_LOGI("","SAMPLER: no sample assigned");
    return;
  }
}

inline void SamplerEngine::noteOff(uint8_t midiNote, Adsr::eEnd_t end_type ){
  if (midiNote > 127) return;
  if (_keyboard[midiNote].noteoff || end_type!= Adsr::END_REGULAR) {
    for (int i = 0 ; i < MAX_POLYPHONY ; i++) {
      if (Voices[i].getMidiNote() == midiNote && Voices[i].isActive()) {      
        // ESP_LOGI("","SAMPLER: NOTE OFF Voice %d note %d ", i, midiNote);
        Voices[i].setPressed(false);
        Voices[i].end(end_type);
      }
    }
  }
}


inline void SamplerEngine::setSustain(bool onoff) {
  _sustain = onoff; 
  // ESP_LOGI("","SAMPLER: sustain: %d", onoff);
  if (!onoff) {
    for (int i = 0 ; i < MAX_POLYPHONY ; i++) {
      if (Voices[i].isActive() && Voices[i].getMidiNote() < 128 && _keyboard[Voices[i].getMidiNote()].noteoff) {
        Voices[i].end(Adsr::END_REGULAR);   
      }
    }
  }
}


void SamplerEngine::setAttackTime(float val) {
  for (int i = 0 ; i < 128; i++ ) {
    _keyboard[i].attack_time = val;
  }
#ifdef ADSR_LIVE_UPDATE
  for (int i = 0 ; i < MAX_POLYPHONY ; i++) {
    Voices[i].setAttackTime(val);
  }
#endif
}

void SamplerEngine::setDecayTime(float val) {
  for (int i = 0; i < 128; i++) {
    _keyboard[i].decay_time = val;
  }
#ifdef ADSR_LIVE_UPDATE
  for (int i = 0 ; i < MAX_POLYPHONY ; i++) {
    Voices[i].setDecayTime(val);
  }
#endif
}

void SamplerEngine::setReleaseTime(float val) {
  for (int i = 0; i < 128; i++) {
    _keyboard[i].release_time = val;
  }
#ifdef ADSR_LIVE_UPDATE
  for (int i = 0 ; i < MAX_POLYPHONY ; i++) {
    Voices[i].setReleaseTime(val);
  }
#endif
}

void SamplerEngine::setSustainLevel(float val) {
  for (int i = 0; i < 128; i++) {
    _keyboard[i].sustain_level = val;
  }
#ifdef ADSR_LIVE_UPDATE
  for (int i = 0 ; i < MAX_POLYPHONY ; i++) {
    Voices[i].setSustainLevel(val);
  }
#endif
}

uint8_t SamplerEngine::mapVelo(uint8_t velo) {
  switch(_veloCurve) {
    case VC_LINEAR:
      return (uint8_t)((float)velo * (float)_veloLayers * DIV_128);
    case VC_CUSTOM:
      return _veloMap[velo];
    default: 
      return _veloLayers-1;
    return 0;
  }
}

uint8_t SamplerEngine::unMapVelo(uint8_t mappedVelo) {
  switch(_veloCurve) {
    case VC_LINEAR:
      return (uint8_t)(127.0 * (float)mappedVelo * _divVeloLayers) ;
    default: 
      return 90;
    return 0;
  }
}

void IRAM_ATTR SamplerEngine::fillBuffer() {
  // search and fill the most hungry buffer
  uint32_t hungerMax;
  uint32_t hunger;
  int iToFeed;
  hunger = hungerMax = 0;
  iToFeed = 0;
  for (int i=0; i<_allocatedVoices; i++) {
    hunger = Voices[i].hunger();
    if (hunger > hungerMax) {
      hungerMax = hunger;
      iToFeed = i;
    }
  }
  if (hungerMax) Voices[iToFeed].feed(); 
  // ESP_LOGI("","SAMPLER: Fed voice id=%d hunger=%d", iToFeed, hunger);
}


inline void SamplerEngine::setCurrentFolder(int folder_id) {
  resetSamples();
  folder_id = constrain(folder_id, 0, _sampleSetsCount-1); // just in case
  _currentFolder = _folders[folder_id];
  _currentFolderId = folder_id;
  _Card->setCurrentDir(_rootFolder);
  ESP_LOGI("", " folder_id %d name: %s", folder_id , _folders[folder_id].c_str());
  _Card->setCurrentDir(_folders[folder_id]);
  initKeyboard();               // it resets _keyboard[] which holds key-specific parameters
  parseIni();                   // this will read the sampler.ini file and prepare name template along with other parameters
  _Card->rewindDir();
  while (true) {                // iterate thru the selected directory
    entry_t* entry = _Card->nextEntry();
    if (entry->is_end) break;
    if (!entry->is_dir) {
      processNameParser(entry); // parse filenames basing on a prepared template
    }
  }
  finalizeMapping();  // fill the gaps when we don't have dedicated samples for some pitches or velocity layers
  ESP_LOGI("SAMPLER", "mapping: sources=%u cell=%u table=%u bytes",
           unsigned(_sampleSources.size()), unsigned(sizeof(sample_t)),
           unsigned(sizeof(_sampleMap)));
 // printMapping();
}


inline void SamplerEngine::setNextFolder() {
  _currentFolderId++;
  if (_currentFolderId > _sampleSetsCount-1) _currentFolderId = 0;
  setCurrentFolder(_currentFolderId);
}

inline void SamplerEngine::setPrevFolder() {
  _currentFolderId--;
  if (_currentFolderId < 0) _currentFolderId = _sampleSetsCount - 1;
  setCurrentFolder(_currentFolderId);
}

void SamplerEngine::initKeyboard() {
  for (int i=0; i<128; ++i) {
    _keyboard[i] = midikey_t{};
    _keyboard[i].freq         = (440.0f / 32.0f) * pow(2, ((float)(i - 9) / 12.0f));
    _keyboard[i].octave       = (i / 12) -1;
    _keyboard[i].name[0]      = notes[0][i%12];
    _keyboard[i].name[1]      = notes[1][i%12];
    _keyboard[i].transpose    = 0;
    _keyboard[i].noteoff      = true;
    //_keyboard[i].velo_layer   = 1;
    _keyboard[i].tuning       = 1.0f;
    // ESP_LOGI("","%d:\t%s\t%s\t%d\t%7.3f", i, _keyboard[i].name[0].c_str(), _keyboard[i].name[1].c_str(), _keyboard[i].octave, _keyboard[i].freq);
  }
}

void SamplerEngine::resetSamples() {
  _title = "";
  _maxVoices = _allocatedVoices;
  _adaptivePolyphony.reset(_allocatedVoices, getUnderruns(), _cpuPressure.load(), millis());
  _sustain = false;
  _amp = 1.0;
  _attackTime   = 0.0f;
  _decayTime    = 0.1f;
  _sustainLevel = 1.0f;
  _releaseTime  = 8.0f;
  for (int i = 0 ; i < MAX_POLYPHONY ; i++) {
    Voices[i].end(Adsr::END_NOW);    
  }
  // Voices hold pointers into this vector; invalidate them only after every
  // renderer has completed its END_NOW handshake.
  _sampleSources.clear();
  for (int i = 0; i<MAX_VELOCITY_LAYERS; i++) {
    for (int j = 0; j<128; j++) {
      _sampleMap[j][i] = sample_t{};
    }
  }
}

int SamplerEngine::getActiveVoices() {
  int n=0;
  for (int i = 0 ; i < MAX_POLYPHONY ; i++) {
    if (Voices[i].isActive()) n++;    
  }
  return n;
}

void SamplerEngine::getSample(float& sampleL, float& sampleR){
  float sL = 0.0f;
  float sR = 0.0f;
  sampleL = 0.0f;
  sampleR = 0.0f;
  for (int i = 0; i < _allocatedVoices; i++) {
    Voices[i].getSample(sL, sR);
    sampleL = sampleL + sL;
    sampleR = sampleR + sR;    
  }  
}

void SamplerEngine::freeSomeVoices() {
  int id = -1, n = 0;
  byte note_count[128]; // better be a private class var maybe
  int midi_note;
  int desiredFree = min(SACRIFY_VOICES, int(_maxVoices) - 1);
  float score;
  float maxKillScore = 0.0f;
  float maxSameKillScore = 0.0f;
  memset(note_count, 0, 128);
  for (int i = 0 ; i < MAX_POLYPHONY ; i++) {
    if (Voices[i].isActive() && !Voices[i].isDying() ) {
      n++;
      midi_note = Voices[i].getMidiNote();
      note_count[midi_note]++;
      if (note_count[midi_note] > _keyboard[midi_note].limit_same) { // if we have limit overrun, find the best candidate
        id = -1;
        for (int j = 0 ; j < MAX_POLYPHONY ; j++) {
          if (Voices[j].isActive() && !Voices[j].isDying() && Voices[j].getMidiNote() == midi_note) {
            score = Voices[j].getKillScore();
            if (id < 0 || score > maxSameKillScore) {
              maxSameKillScore = score;
              id = j;
            }
          }
        }
        if (id >= 0) {
          ++_stealsSameNote;
          Voices[id].end(Adsr::END_FAST);
        }
        //ESP_LOGI("","SAMPLER: KILL SAME NOTE id=%d", id);
        return;
      }
      score = Voices[i].getKillScore();
      if (id < 0 || score > maxKillScore) {
        maxKillScore = score;
        id = i;
      }
    }
  }
  if ( ( n + desiredFree ) > _maxVoices ) {
    if (id >= 0) {
      ++_stealsPolyphony;
      Voices[id].end(Adsr::END_FAST);
    }
    //ESP_LOGI("","SAMPLER: KILL EXTRA VOICE id=%d", id);
    return;
  }
}


inline void SamplerEngine::setPitch(int number) {
  float speedModifier = ((((float)number + 8191.5f) * (float)TWO_DIV_16383 ) - 1.0f ) * (float)_pitchBendSemitones;
  speedModifier = fast_semitones2speed(speedModifier);
  for (int i=0; i<_allocatedVoices; i++) {
    Voices[i].setPitch(speedModifier);
  }
}

void SamplerEngine::setMaxVoices(byte value) {
  const uint8_t cap = constrain(value, 1, _allocatedVoices);
  _adaptivePolyphony.reset(cap, getUnderruns(), _cpuPressure.load(), millis());
  _maxVoices = cap;
}

void SamplerEngine::updateAdaptivePolyphony() {
  if (_adaptivePolyphony.update(getUnderruns(), _cpuPressure.load(), millis()))
    _maxVoices = _adaptivePolyphony.limit();
}

void SamplerEngine::allNotesOff(bool immediate) {
  for (int i = 0; i < _allocatedVoices; ++i) {
    Voices[i].setPressed(false);
    Voices[i].end(immediate ? Adsr::END_NOW : Adsr::END_REGULAR);
  }
}

void SamplerEngine::printStreamStats() {
  uint32_t underruns = 0, slowest = 0;
  for (int i = 0; i < _allocatedVoices; ++i) {
    underruns += Voices[i].underruns();
    slowest = max(slowest, Voices[i].maxFeedMicros());
  }
  ESP_LOGI("STREAM", "underruns=%lu max_refill_us=%lu", (unsigned long)underruns, (unsigned long)slowest);
  ESP_LOGI("STREAM", "voices=%d limit=%u/%u allocated=%u cpu_pressure=%lu steal_limit=%lu steal_same=%lu steal_immediate=%lu",
           getActiveVoices(), unsigned(_maxVoices), unsigned(_adaptivePolyphony.cap()), unsigned(_allocatedVoices),
           (unsigned long)_cpuPressure.load(),
           (unsigned long)_stealsPolyphony, (unsigned long)_stealsSameNote,
           (unsigned long)_stealsImmediate);
#if DEBUG_AUDIO_DIAGNOSTICS
  printAudioDiagnostics();
#endif
}

bool SamplerEngine::needsRefill() {
  for (int i = 0; i < _allocatedVoices; ++i)
    if (Voices[i].hunger()) return true;
  return false;
}

uint32_t SamplerEngine::getUnderruns() const {
  uint32_t count = 0;
  for (int i = 0; i < _allocatedVoices; ++i) count += Voices[i].underruns();
  return count;
}
