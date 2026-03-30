#include "sampler.h"
#include "sdmmc_file.h"

void SamplerEngine::parseIni() {
  eSection_t section = S_NONE;
  _veloCurve = VC_LINEAR;
  ini_range_t range;
  _template.clear();
  _ranges.clear();
  variants_t grps;
  for (int i = 0 ; i < 128; i++) {
    for (int j = 0; j < ( ( MAX_NOTES_PER_GROUP - 1 ) * MAX_GROUPS_CROSSES ); j++) {
      _groups[i][j] = 255; // empty groups
    }
  }
  _Card->rewindDir();
  int i, a, b;
  str256_t iniStr;
  str20_t tok;
  _parser_i = 0;
  ESP_LOGI("","Reading ini file");
  SDMMC_FileReader Reader(_Card);
  Reader.open(INI_FILE);
  while (Reader.available()) {
    Reader.read_line(iniStr);
    ESP_LOGI("", "%s", iniStr.c_str()); 
    iniStr.trim();
    iniStr.toUpperCase();
    if (iniStr.empty()) continue;
    else if (iniStr.startsWith("#")) continue;                      // comment
    else if (iniStr.startsWith(";")) continue;                      // comment
    if (iniStr.startsWith("[") && iniStr.endsWith("]")) {           // new section
      if (section==S_NOTE || section==S_RANGE) applyRange(range);   // save parsed range/note section
      range.clear(_type);
      section = parseSection(iniStr);
      //ESP_LOGI("",section);
      continue;
    }
    int s1 = iniStr.indexOf('='); // leftmost of ":" or "=" will be treated as left and right parts delimeter
    int s2 = iniStr.indexOf(':');
    if ( s1<=0 && s2<=0 ) continue;
    if ( s1>0 && s2>0 ) {
      if ( s1<s2 ) {
        tok = iniStr.substring(0, s1);
        iniStr.remove(0, s1+1);
      } else {
        tok = iniStr.substring(0, s2);
        iniStr.remove(0, s2+1);
      } 
    }
    if ( s2<=0 ) { // s1>0
      tok = iniStr.substring(0, s1);
      iniStr.remove(0, s1+1);
    }
    if ( s1<=0 ) { // s2>0
      tok = iniStr.substring(0, s2);
      iniStr.remove(0, s2+1);
    } 
    tok.trim();
    iniStr.trim();    
    switch (section) {
      case S_FILENAME:
        // template
        if (tok == "TEMPLATE") { parseFilenameTemplate(iniStr) ; continue; }
        // veloVariants
        if (tok == "VELOVARIANTS" || tok == "VELO_VARIANTS") {_veloVars = parseVariants(iniStr); continue; }
        // veloLimits
        if (tok == "VELOLIMITS" || tok == "VELO_LIMITS") {
          _veloCurve = VC_CUSTOM;
          parseLimits(iniStr); continue; 
        }
        break;
      case S_NOTE:
      case S_RANGE:
        if (tok == "NAME") {range.first = midiNoteByName(iniStr); range.last = range.first; continue;}
        if (tok == "FIRST" || tok == "FIRST_NOTE" || tok == "FIRSTNOTE") {range.first = midiNoteByName(iniStr); continue;}
        if (tok == "LAST" || tok == "LAST_NOTE" || tok == "LASTNOTE") {range.last = midiNoteByName(iniStr); continue;}
        if (tok == "INSTR") {range.instr = iniStr; continue;}
        if (tok == "NOTEOFF" || tok == "NOTE_OFF") {range.noteoff = parseBoolValue(iniStr); continue;}
        if (tok == "SPEED") {range.speed = parseFloatValue(iniStr); continue;}
        if (tok == "LIMIT_SAME_NOTES" || tok == "LIMIT_SAME_NOTE" || tok == "LIMITSAMENOTE" || tok == "LIMITSAMENOTES") {
           range.limit_same = min(MAX_POLYPHONY, parseIntValue(iniStr));
           if (range.limit_same == 0 ) range.limit_same = MAX_POLYPHONY;
           continue;
        }
        if (tok == "ATTACKTIME" || tok == "ATTACK_TIME") {range.attack_time = parseFloatValue(iniStr); continue;}
        if (tok == "DECAYTIME" || tok == "DECAY_TIME") {range.decay_time = parseFloatValue(iniStr); continue;}
        if (tok == "RELEASETIME" || tok == "RELEASE_TIME") {range.release_time = parseFloatValue(iniStr); continue;}
        if (tok == "SUSTAINLEVEL" || tok == "SUSTAIN_LEVEL") {range.sustain_level = parseFloatValue(iniStr); continue;}
        if (tok == "LOOP" || tok == "AUTOREPEAT" || tok == "REPEAT" || tok == "CYCLE" ) {range.loop = parseBoolValue(iniStr); continue;}
        break;
      case S_GROUP:
        if (tok == "NOTES") {grps = parseVariants(iniStr); storeGroup(grps); continue; }
        break;
      case S_NONE:
      case S_SAMPLESET:
      default:
        if (tok == "TITLE") {_title = iniStr; continue;}
        if (tok == "TYPE") {
          _type = parseInstrType(iniStr);
          for (int i=0; i<128; ++i) {
              _keyboard[i].noteoff = (_type!=SMP_PERCUSSIVE);
          }
          continue;
        }
        if (tok == "LOOP" || tok == "AUTOREPEAT" || tok == "REPEAT" || tok == "CYCLE" ) {
          bool bVal = parseBoolValue(iniStr);
          for (int i=0; i<128; ++i) {
              _keyboard[i].loop = bVal;
          }
          continue;
        }
        if (tok == "NORMALIZED") {_normalized = parseBoolValue(iniStr); continue;}
        if (tok == "AMP" || tok == "AMPLIFY") {_amp = parseFloatValue(iniStr); continue;}
        if (tok == "LIMIT_SAME_NOTES" || tok == "LIMIT_SAME_NOTE" || tok == "LIMITSAMENOTE" || tok == "LIMITSAMENOTES") {
          _limitSameNotes = min(MAX_POLYPHONY, parseIntValue(iniStr));
          if (_limitSameNotes == 0) _limitSameNotes = MAX_POLYPHONY;
          for (int i=0; i<128; ++i) {
              _keyboard[i].limit_same = _limitSameNotes;
          }
          continue;
        }
        if (tok == "MAX_VOICES" || tok == "MAX_POLYPHONY" || tok == "MAXPOLYPHONY" || tok == "MAXVOICES" || tok == "POLYPHONY") {_maxVoices = min(MAX_POLYPHONY, parseIntValue(iniStr)); continue;}
        if (tok == "ATTACKTIME" || tok == "ATTACK_TIME") {_attackTime = parseFloatValue(iniStr); setAttackTime(_attackTime); continue;}
        if (tok == "DECAYTIME" || tok == "DECAY_TIME") {_decayTime = parseFloatValue(iniStr); setDecayTime(_decayTime); continue;}
        if (tok == "RELEASETIME" || tok == "RELEASE_TIME") {_releaseTime = parseFloatValue(iniStr); setReleaseTime(_releaseTime); continue;}
        if (tok == "SUSTAINLEVEL" || tok == "SUSTAIN_LEVEL") {_sustainLevel = parseFloatValue(iniStr); setSustainLevel(_sustainLevel); continue;}
        break;
    }    
  }
  // save parsed section
  if (section==S_NOTE || section==S_RANGE) applyRange(range);
  Reader.close();
  ESP_LOGI("","SAMPLER: INI: PARSING COMPLETE");
  //delay(1000);
}

eSection_t SamplerEngine::parseSection( str256_t& val ) {
  //ESP_LOGI("",val.c_str());
  int len = val.length();
  val.remove(len-1);  // remove closing "]" 
  val.remove(0,1);    // remove opening "["
  val.trim();         // trim the inner string
  if (val == "SAMPLESET") return S_SAMPLESET;
  if (val == "FILENAME")  return S_FILENAME;
  if (val == "NOTE")      return S_NOTE;
  if (val == "RANGE")     return S_RANGE;
  if (val == "GROUP")     return S_GROUP;
  return S_NONE;
}

bool SamplerEngine::parseBoolValue( str256_t& val ) {
  val.trim();
  val.toUpperCase();
  bool var = false;
  if (val=="TRUE" || val=="YES" || val=="Y" || val=="1") var = true;
  if (val=="FALSE" || val=="NO" || val=="NONE" || val=="N" || val=="0") var = false;
  return var;
}


float SamplerEngine::parseFloatValue( str256_t& val ) {
  val.trim();
  val.toUpperCase();
  float f = val.toFloat();
  ESP_LOGI("","INI: float %f", f);
  return f;
}


int SamplerEngine::parseIntValue( str256_t& val ) {
  val.trim();
  val.toUpperCase();
  int i = val.toInt();
  ESP_LOGI("","INI: int %d", i);
  return i;
}


variants_t SamplerEngine::parseVariants( str256_t& val ) {
  ESP_LOGI("","INI: Parsing variants");
  variants_t vars;
  vars.clear();
  val.trim();
  val.toUpperCase();
  int j = 0;
  int k = 0;
  while (true) {
    str20_t s1;
    k = val.indexOf("," , j);
    if (k == -1 || k < j) k = val.length();
    s1 = val.substring(j, k);
    s1.trim();
    vars.push_back(s1);
    ESP_LOGI("","Adding %s" , s1.c_str());
    if (k == val.length()) break;
    j = k + 1;
  }
  return vars;
}


void SamplerEngine::storeGroup( variants_t& vars ) {
  uint8_t midi_note[MAX_NOTES_PER_GROUP];
  int i=0;
  uint8_t a_note;
  for (auto & str: vars) {
    midi_note[i] = midiNoteByName(str);
    i++;
  }
  ESP_LOGI("",);
  for (int n = 0 ; n < i; n++) {
    a_note = midi_note[n];
 //   ESP_LOGI("","INI: Placing group: midi_note %d", a_note);
    for (int m = 0 ; m < ( ( MAX_NOTES_PER_GROUP - 1 ) * MAX_GROUPS_CROSSES ); m++) {
      if (_groups[a_note][m] == 255) { // found an empty element
        for (int j = 0; j < i; j++) {
          if (midi_note[j] != a_note) {
 //           ESP_LOGI("","------- adding %d", midi_note[j]);
            _groups[a_note][m] = midi_note[j];
            m++;
          }
        }
        break;
      }
    }
  }
} 


void SamplerEngine::parseLimits( str256_t& val) {
  ESP_LOGI("","INI: Parsing velo limits");
  variants_t vars;
  vars.clear();
  val.trim();
  int j = 0;
  int k = 0;
  int lim1 = 0;
  int lim2 = 0;
  int layer = 0;
  while (true) {
    str20_t s1;
    k = val.indexOf("," , j);
    if (k == -1 || k < j) k = val.length();
    s1 = val.substring(j, k);
    s1.trim();
    lim2 = s1.toInt();
    for (int i = lim1; i <= lim2; i++) { _veloMap[i] = layer; }
    ESP_LOGI("","Adding %s" , s1.c_str());
    layer++;
    lim1 = lim2 + 1;
    if (k == val.length()) break;
    if (lim1 > 127) break;
    j = k + 1;
  }
  _veloCurve = VC_CUSTOM;
}

eInstr_t SamplerEngine::parseInstrType(str256_t& val) {
  val.trim();
  val.toUpperCase(); 
  if (val == "MELODIC") return SMP_MELODIC;
  if (val == "PERCUSSIVE") return SMP_PERCUSSIVE;
  return SMP_MELODIC; // by default
}


void SamplerEngine::applyRange(ini_range_t& range) {
  _ranges.push_back(range);
  for (int i=range.first; i<=range.last; i++) {
    _keyboard[i].noteoff        = range.noteoff;
    _keyboard[i].tuning         = range.speed;
    _keyboard[i].limit_same     = range.limit_same;
    _keyboard[i].attack_time    = range.attack_time;
    _keyboard[i].decay_time     = range.decay_time;
    _keyboard[i].sustain_level  = range.sustain_level;
    _keyboard[i].release_time   = range.release_time;
  }
  ESP_LOGI("","INI: adding range for %s", range.instr.c_str());
}


bool SamplerEngine::parseFilenameTemplate(str256_t& line) {
  ESP_LOGI("","Parse filename template:[[[%s]]]", line.c_str());
  int a, b, i;
  str256_t str;
  template_item_t item;
  i = 0;
  while (true) {
    a = line.indexOf("<",i);
    b = line.indexOf(">",i);
    if (a < 0  ||  b < 0) break;
    if (b < a) return false;
    if (a > i && b > a) {  // at the start of separator
      str = line.substring(i, a );
      ESP_LOGI("","Parsing filename template: String separator: [%s]", str.c_str());
      item.item_type = P_SEPARATOR;
      item.item_str = str;
      _template.push_back(item);
      i = a;
    }
    if (a == i) { // at the start of token
      str = line.substring(a + 1, b );
      ESP_LOGI("","Parsing filename template: Token: [%s]", str.c_str());
      str = str.substring(0, 4);
      if (str ==  "NUMB" ) {
          item.item_type = P_NUMBER;
          item.item_str = "";
          _template.push_back(item);
      }
      else if (str == "NAME" ) {
          item.item_type = P_NAME;
          item.item_str = "";
          _template.push_back(item);
      }
      else if (str == "MIDI" ) {
          item.item_type = P_MIDINOTE;
          item.item_str = "";
          _template.push_back(item);
      }
      else if (str == "OCTA" ) {
          item.item_type = P_OCTAVE;
          item.item_str = "";
          _template.push_back(item);
      }
      else if (str == "VELO" ) {
          item.item_type = P_VELO;
          item.item_str = "";
          _template.push_back(item);    
      }
      else if (str == "INST" ) {
          item.item_type = P_INSTRUMENT;
          item.item_str = "";
          _template.push_back(item);    
      }
    }
    i = b + 1;
    if (i >= line.length()) return true;
  }
  return true;
}


void SamplerEngine::processNameParser(entry_t* entry) {
  int pos = 0;
  int i = 0;
  int range_i = 0;
  int len = 0;
  int oct = -2;
  int velo = 0;
  int note_num = -1;
  int midi_note_num = -1;
  std::vector<int> rng_i; // affected indices
  int match_weight = 0;
  str20_t instr = "";
  str20_t s;
  sample_t smp;
  fname_t fname = entry->name;
  fname.toUpperCase();
  if (fname.endsWith(".WAV")) {
    //ESP_LOGI("","Parsing name: <%s>", fname.c_str());
  } else {
    ESP_LOGI("","Skipping name: <%s>", fname.c_str());
    return;
  }
  for (auto tpl: _template) {
    switch ((int)tpl.item_type) {
      case P_NAME: // note name
        match_weight = 0;
        for (int j = 0; j < 2 ; j++) {
          for (int k = 0 ; k < 12; k++) {
            str8_t a = notes[j][k];
            len = a.length();
            str8_t b = fname.substring(pos, pos + len);
          //  ESP_LOGI("","compare: <%s> <%s>", a.c_str() , b.c_str());
            if (a.equalsIgnoreCase(b) && len > match_weight) {
              match_weight = len;
              note_num = k;
            }
          }
        }
        if ( note_num >= 0 ) {
          pos += match_weight;
        }
        else {
          ESP_LOGI("","Failed to determine note name in <%s>", fname.c_str());
        }
      //  ESP_LOGI("","Note: %s", notes[0][note_num].c_str());
        break;
      case P_NUMBER:
        s = "";
        while (true) {
          if (fname.charAt(pos) >='0' && fname.charAt(pos) <='9' ) {
            s += fname.charAt(pos);
            pos++;
          } else {
//            ESP_LOGI("","Number: %s", s.c_str());
            break;
          }
        }
        break;
      case P_MIDINOTE:
        s = "";
        while (true) {
          if (fname.charAt(pos) >='0' && fname.charAt(pos) <='9' ) {
            s += fname.charAt(pos);
            pos++;
          } else {
            break;
          }
        }
        //ESP_LOGI("","Midi Note Number: %s", s.c_str());
        if (s>"") midi_note_num = s.toInt();
        break;
      case P_INSTRUMENT: {
       // ESP_LOGI("","Filename: Parsing P_INSTRUMENT");
          i = 0;
          match_weight = 0;
          for (auto rng: _ranges) { // first pass: find max match_weight
            len = rng.instr.length();
            instr = fname.substring(pos, pos + len);
            // ESP_LOGI("","INI: TEST instr: [%s] ", instr.c_str());
            if (len>match_weight && instr.equalsIgnoreCase(rng.instr)) {
              match_weight = len;
            }
          }
          i = 0;
          rng_i.clear();
          for (auto rng: _ranges) { // second pass: we can have more than 1 match
            len = rng.instr.length();
            if (len == match_weight) {
              instr = fname.substring(pos, pos + len);
              if (instr.equalsIgnoreCase(rng.instr)) {
                // range matches by instrument
                rng_i.push_back(i);
                 ESP_LOGI("","INI: Matched instr: %s, %d ", instr.c_str(), i);
              }
            }
            i++;
          }
          if (match_weight>0) {
            pos += match_weight;
          } else {
            ESP_LOGI("","INI: Couldn't match instrument name [%s] in %s", instr.c_str(), fname.c_str());    
          }
        }
      case P_OCTAVE:
        s = fname.substring(pos, pos+1);
        oct = s.toInt();
//        ESP_LOGI("","Octave: %d" , oct);
        pos++;
        break;
      case P_VELO:
      // ESP_LOGI("","Filename: Parsing P_VELO");
        i = 0;
        match_weight = 0;
        for(auto var: _veloVars) {
          len = var.length();
          s = fname.substring(pos, pos + len);
          if (len>match_weight && s.equalsIgnoreCase(var)) {
            match_weight = len;
            velo = i;
          }
          i++;
        }
        // ESP_LOGI("","Velocity: %d", velo);
        pos += match_weight;
        break;
      case P_SEPARATOR:
        s = tpl.item_str;
        len = s.length();
        // ESP_LOGI("","Skipping separator %s", s.c_str());
        pos += len;
        break;
      default:
        //impossible:
      break;
    }
  }
// if we have note name in filename
  if (note_num>=0 && oct>=-1) {
    midi_note_num = note_num + (oct+1)*12; // midi note number
  }  
  if (midi_note_num>=0) {
    if ( velo >= 0 ) {
      sample_t smp;
      smp.orig_velo_layer = velo;
      smp.sectors = entry->sectors;
      smp.size = entry->size;
      smp.native_freq = true;
      // smp.name = (entry->name);
      parseWavHeader(entry, smp);
      _sampleMap[midi_note_num][velo] = smp;
    }
  }

// if we have [ranges] or [notes] in ini
  if ( !rng_i.empty() ) {
    if (velo < 0) velo = 0;
    for (auto ix: rng_i) {
      for (int midi_note = _ranges[ix].first; midi_note<=_ranges[ix].last; midi_note++) {
        sample_t smp;
        smp.orig_velo_layer = velo;
        smp.sectors = entry->sectors;
        smp.size = entry->size;
        smp.native_freq = true;
        // smp.name = (entry->name);
        parseWavHeader(entry, smp);
        _sampleMap[midi_note][velo] = smp;
      }
    }
  }
}


void SamplerEngine::parseWavHeader(entry_t* wav_entry, sample_t& smp){
  char* buf = reinterpret_cast<char*>(_Card->readFirstSector(wav_entry)); // massive long headers won't pass here (
  wav_header_t* wav = reinterpret_cast<wav_header_t*>(buf);
  smp.sample_rate = wav->sampleRate;
  smp.bit_depth = wav->bitsPerSample;
  smp.channels = wav->numberOfChannels;
  smp.speed = (float)(wav->sampleRate) * DIV_SAMPLE_RATE;
  int res = -1;
  for (int i = 0 ; i < BYTES_PER_SECTOR-4; i++) {
    if (buf[i]=='d' && buf[i+1]=='a' && buf[i+2]=='t' && buf[i+3]=='a') {
      res = i;
      break;
    }
  }
  if (res >= 0 ) {
    smp.byte_offset = res + 8;
    smp.data_size = *(reinterpret_cast<uint32_t*>(&buf[res+4]));
  }
}


// fill gaps
void SamplerEngine::finalizeMapping() {
  int x, y, dx, dy, xc, yc, s;
  int max_v = 0;
  int n = 1 + 2 * MAX_DISTANCE_STRETCH;
  sample_t smp;
  _veloLayers = 1;
  // first pass: determine the number of velocity layers used
  for (int i = 0; i < MAX_VELOCITY_LAYERS; i++) {
    for (int j = 0; j < 128; j++) {
      if (_sampleMap[j][i].bit_depth > 0) {
        _veloLayers = i + 1;
        break;
      }
    }
  }
  _divVeloLayers = 1.0f / (float)_veloLayers;
  switch((int)_type) {
    case SMP_PERCUSSIVE:
      for (int i = 0; i < 128; i++) {
        smp.bit_depth = 0;
        for (int j = 0; j < _veloLayers; j++) {
          if (_sampleMap[i][j].bit_depth > 0) {
            smp = _sampleMap[i][j];
          } else {
            if (smp.bit_depth > 0) {
              _sampleMap[i][j] = smp;
            }
          }
        }
        for (int j = _veloLayers-1; j >=0; j--) {
          if (_sampleMap[i][j].bit_depth > 0) {
            smp = _sampleMap[i][j];
          } else {
            if (smp.bit_depth > 0) {
              _sampleMap[i][j] = smp;
            }
          }
        }
      }
      break;
    case SMP_MELODIC:
    default: {
      // second pass
      for (int i = 0; i < _veloLayers; i++) {
        // going up
        for (int j = 0; j < 128; j++) {
          if (_sampleMap[j][i].speed == 0.0f) {
            // search in _sampleMap by spiral
            s = -1;
            x = j;
            y = unMapVelo(i) ;
            for (int k = 1; k <= n; k++) {
              dx = s;
              dy = 0;
              for (int m = 0 ; m < k; m++) {
                x += dx;
                y += dy;
                xc = constrain(x, 0, 127);
                yc = mapVelo(constrain(y, 0, 127));
                if (_sampleMap[xc][yc].speed > 0.0f ) {
                  _sampleMap[j][i] = _sampleMap[xc][yc];
                  _sampleMap[j][i].speed = _sampleMap[xc][yc].speed * _keyboard[j].freq / _keyboard[x].freq;
                  _sampleMap[j][i].native_freq = false;
                  break;  
                }
              }        
              s = -s;
              dx = 0;
              dy = s;  
              for (int m = 0 ; m < k; m++) {
                x += dx;
                y += dy;
                xc = constrain(x, 0, 127);
                yc = mapVelo(constrain(y, 0, 127));
                if (_sampleMap[xc][yc].native_freq) {
                  _sampleMap[j][i] = _sampleMap[xc][yc];
                  _sampleMap[j][i].speed = _sampleMap[xc][yc].speed * _keyboard[j].freq / _keyboard[x].freq;
                  _sampleMap[j][i].native_freq = false;
                  break;  
                }
              } 
            }
          }
        }
      }
    }
  }
  // propagate params to individual samples
  for (int i = 0; i < _veloLayers; i++) {
    for (int j = 0 ; j < 128; j++ ) {
      _sampleMap[j][i].speed         *= _keyboard[j].tuning;
      _sampleMap[j][i].amp           *= _amp;
      /*
      _sampleMap[j][i].attack_time    = _keyboard[j].attack_time;
      _sampleMap[j][i].decay_time     = _keyboard[j].decay_time;
      _sampleMap[j][i].sustain_level  = _keyboard[j].sustain_level;
      _sampleMap[j][i].release_time   = _keyboard[j].release_time;
      */
    }
  }
}


uint8_t SamplerEngine::midiNoteByName(str8_t noteNameOct) {
  int match_weight = 0;
  int len, note_num = -1;
  int oct = -3;
  for (int j = 0; j < 2 ; j++) {
    for (int k = 0 ; k < 12; k++) {
      str8_t a = notes[j][k];
      len = a.length();
      str8_t b = noteNameOct.substring(0,len);
      if (a.equalsIgnoreCase(b) && len > match_weight) {
        match_weight = len;
        note_num = k;
      }
    }
  }
  str8_t b = noteNameOct.substring(match_weight, match_weight+2);
 // ESP_LOGI("","INI: midiNoteByName string: [%s] note: [%d] oct: [%s] ", noteNameOct.c_str(), note_num, b.c_str());
  oct = b.toInt();
  if (oct>-2) {
    note_num+=(oct+1)*12;
  }
 // ESP_LOGI("",note_num);
  return note_num;
}


void SamplerEngine::printMapping() {
  for (int i = 0; i < 128; i++) {
    ESP_LOGI("","%s%d\t", notes[1][i%12].c_str(), i/12 -1 );
  }
  ESP_LOGI("",",");
  for (int i = 0; i < _veloLayers; i++) {
    for (int j = 0 ; j<128; j++ ) {
      if (!_sampleMap[j][i].sectors.empty()) {
        ESP_LOGI("","%d %3.2f\t",_sampleMap[j][i].native_freq, _sampleMap[j][i].speed );
      //  ESP_LOGI("","%s\t", _sampleMap[j][i].name.c_str() );
      } else {        
        ESP_LOGI("", "%d\t", 0 );
      }
    }
    ESP_LOGI("",".");
  }

  for (int j = 0; j < ( ( MAX_NOTES_PER_GROUP - 1 ) * MAX_GROUPS_CROSSES ); j++) {
    for (int i = 0 ; i < 128; i++) {
       ESP_LOGI("","%d\t", _groups[i][j] );
    }
    ESP_LOGI("",".");
  }

  
}
