#include "sdmmc_file.h"
#include "sdmmc.h"

SDMMC_FileReader::SDMMC_FileReader( SDMMC_FAT32* Card ) {
  _Card = Card;
}

esp_err_t SDMMC_FileReader::open(fpath_t fname) {
  _filePos = 0;
  _bufCount = 0;
  _bufPos = 0;
  entry_t* entry_p = _Card->findEntry(fname);
 
  //ESP_LOGI("","  &entry_p = %#010x\r\n",  entry_p);
  _entry = *entry_p;
  if (_entry.is_end) return 0x105; // NOT_FOUND
  return 0 ; // ESP_OK
}

esp_err_t SDMMC_FileReader::close() {
  _entry.is_end = 1;
  return 0 ; // ESP_OK;
}

bool SDMMC_FileReader::available() {
  if (_entry.is_end == 1) return false;
  if (_filePos>=_entry.size) return false;
  return true;  
}

void SDMMC_FileReader::read_line(str_max_t& str) {
  str = "";
  char ch;
  int str_pos = 0;
  uint32_t sec;
  if (_filePos==0) {
    sec = _entry.sectors[0].first;
    char* tmp = reinterpret_cast<char*>(_Card->readSector(sec));
    memcpy(_sectorBuf, tmp, BYTES_PER_SECTOR);
    _sectorRead = sec;
  }
  while (true) {
    
    if (_bufPos >= BYTES_PER_SECTOR) {
    //  ESP_LOGI("","Next sector");
      _bufPos = 0;
      sec = _Card->getNextSector(_sectorRead);
      char* tmp = reinterpret_cast<char*>(_Card->readSector(sec));
      memcpy(_sectorBuf, tmp, BYTES_PER_SECTOR);
      _sectorRead = sec;
    }
    ch = _sectorBuf[_bufPos];
    if (str_pos >= MAX_STR_LEN-2) {
      str += '\0';
      _filePos++;
      _bufPos++;
     // ESP_LOGI("","FixedString limit");
      break; // FixedString overflow
    }
    if (_filePos >= _entry.size) {
      str += '\0';
      _entry.is_end = 1;
     // ESP_LOGI("","EOF");
      break; // EOF
    }
    if ( ch == '\0' || ch == '\n' ) {
      _bufPos++;
      _filePos++;
      str += '\0';
     // ESP_LOGI("","EOL");
      break; // EOL
    }
    if (ch != '\r') {
      str += ch;
      str_pos++;
    }
    _bufPos++;
    _filePos++;
  }
  str.trim();
 // ESP_LOGI("","position: buf=%d file=%d str=%d \r\n", _bufPos, _filePos, str_pos);
}
