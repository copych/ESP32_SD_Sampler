#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"


// utility functions for other files to import
esp_err_t SDMMC_FAT32::write_block(const void *source, uint32_t block, uint32_t size)
{
  #ifdef USE_MUTEX
  xSemaphoreTake(mutex, portMAX_DELAY);
  #endif
  ret = sdmmc_write_sectors(&card, (const void *)source, (uint32_t)block, (uint32_t)size);
  #ifdef USE_MUTEX
	xSemaphoreGive(mutex);
  #endif
  return(ret);
}

esp_err_t SDMMC_FAT32::read_block(void *dst, uint32_t start_sector, uint32_t sector_count)
{
  #ifdef USE_MUTEX
  xSemaphoreTake(mutex, portMAX_DELAY);
  #endif
  ret = sdmmc_read_sectors(&card, (void *)dst, (uint32_t)start_sector, (uint32_t)sector_count);
  #ifdef USE_MUTEX
	xSemaphoreGive(mutex);
  #endif
  return(ret);

}

esp_err_t SDMMC_FAT32::read_sector( uint32_t sector )
{
  #ifdef USE_MUTEX
  xSemaphoreTake(mutex, portMAX_DELAY);
  #endif
  ret = sdmmc_read_sectors(&card, (void *)sector_buf, (uint32_t)sector, 1UL );
  _sectorInBuf = sector;
  #ifdef USE_MUTEX
	xSemaphoreGive(mutex);
  #endif
  return(ret);
}

esp_err_t SDMMC_FAT32::cache_fat( uint32_t sector )
{
  #ifdef USE_MUTEX
  xSemaphoreTake(mutex, portMAX_DELAY);
  #endif
  ret = sdmmc_read_sectors(&card, (void *)(fat_cache.uint8), (uint32_t)sector, FAT_CACHE_SECTORS );
  _firstCachedFatSector = sector;
  _lastCachedFatSector = sector + FAT_CACHE_SECTORS - 1;
  #ifdef USE_MUTEX
	xSemaphoreGive(mutex);
  #endif
  return(ret);
}

esp_err_t SDMMC_FAT32::cache_dir( uint32_t sector )
{
  #ifdef USE_MUTEX
  xSemaphoreTake(mutex, portMAX_DELAY);
  #endif
  ret = sdmmc_read_sectors(&card, (void *)(dir_cache), (uint32_t)sector, DIR_CACHE_SECTORS );
  _firstCachedDirSector = sector;
  _lastCachedDirSector = sector + DIR_CACHE_SECTORS - 1;
  #ifdef USE_MUTEX
	xSemaphoreGive(mutex);
  #endif
  return(ret);
}

// tool to test hardware with different buffer sizes
void SDMMC_FAT32::testReadSpeed(uint32_t sectorsPerRead, uint32_t totalMB){
  uint32_t readCount = totalMB * 1024 * 1024 / BYTES_PER_SECTOR / sectorsPerRead;
  //auto rcv = static_cast<uint8_t*>(malloc(BYTES_PER_SECTOR*READ_BUF_SECTORS*sizeof(uint8_t)));
  uint8_t rcv[BYTES_PER_SECTOR*READ_BUF_SECTORS]; 
  pinMode(18,INPUT);
  randomSeed(analogRead(18));
  volatile size_t ms1, ms2;
  ms1 = micros();
  for (uint32_t i = 0; i<readCount; ++i) {    
    uint32_t start_sector = random(_sectorsTotal-sectorsPerRead);
    read_block((void*)rcv, start_sector, sectorsPerRead);
  }
  ms2 = micros(); 
  float MBytesPerS = (float)totalMB * 1000.0 * 1000.0 / (float)(ms2-ms1);
  ESP_LOGI("","Reading %d MBytes\r\n", totalMB);
  ESP_LOGI("","Reading block size: %d Bytes\r\n", sectorsPerRead * BYTES_PER_SECTOR);
  ESP_LOGI("","Time spent: %d ms\r\n" , (ms2-ms1)/1000);
  ESP_LOGI("","Read speed: %3.2f MB/s\r\n\r\n" , MBytesPerS);
}
    
void SDMMC_FAT32::begin(void)
{
#ifdef USE_MUTEX
  mutex = xSemaphoreCreateMutex();
#endif
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_4BIT;
  host.flags &= ~SDMMC_HOST_FLAG_DDR;       // DDR mode OFF
  // host.flags |= SDMMC_HOST_FLAG_DDR;          // DDR mode ON
  // host.max_freq_khz = SDMMC_FREQ_52M;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
#if defined SDMMC_D0

  gpio_pulldown_dis((gpio_num_t)SDMMC_D0);
  gpio_pulldown_dis((gpio_num_t)SDMMC_D1);
  gpio_pulldown_dis((gpio_num_t)SDMMC_D2); 
  gpio_pulldown_dis((gpio_num_t)SDMMC_D3);
  gpio_pulldown_dis((gpio_num_t)SDMMC_CLK);
  gpio_pulldown_dis((gpio_num_t)SDMMC_CMD);
  gpio_pullup_en((gpio_num_t)SDMMC_D0);
  gpio_pullup_en((gpio_num_t)SDMMC_D1);
  gpio_pullup_en((gpio_num_t)SDMMC_D3);
  gpio_pullup_en((gpio_num_t)SDMMC_CLK);
  gpio_pullup_en((gpio_num_t)SDMMC_CMD);
 #if defined(CONFIG_IDF_TARGET_ESP32S3)
  gpio_pullup_en((gpio_num_t)SDMMC_D2); // formally this is required, nevertheless GPIO12 of ESP32 set HIGH in some cases leads to a bootloop or a bootstop, so it's up to you if you want it for ESP32
  slot_config.clk = (gpio_num_t)SDMMC_CLK;
  slot_config.cmd = (gpio_num_t)SDMMC_CMD;
  slot_config.d0  = (gpio_num_t)SDMMC_D0;
  slot_config.d1  = (gpio_num_t)SDMMC_D1;
  slot_config.d2  = (gpio_num_t)SDMMC_D2;
  slot_config.d3  = (gpio_num_t)SDMMC_D3;
 #endif
#endif
  
   
  slot_config.width = 4;
  ret = sdmmc_host_init();
  if(ret != ESP_OK) {    ESP_LOGI("", "sdmmc_host_init : %s\r\n", esp_err_to_name(ret));  }
  ret = sdmmc_host_set_bus_ddr_mode(SDMMC_HOST_SLOT_1, false);
  if(ret != ESP_OK) {    ESP_LOGI("", "sdmmc_host_set_bus_ddr_mode : %s\r\n", esp_err_to_name(ret));   }
  ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot_config);
  if(ret != ESP_OK) {    ESP_LOGI("", "sdmmc_host_init_slot : %s\r\n", esp_err_to_name(ret));  }
  ret = sdmmc_card_init(&host, &card);  
  if(ret != ESP_OK) {    ESP_LOGI("", "sdmmc_card_init : %s\r\n", esp_err_to_name(ret));  }
  sdmmc_card_print_info(stdout, &card);
  uint32_t width = sdmmc_host_get_slot_width(SDMMC_HOST_SLOT_1);
  ESP_LOGI("", "Bus width: %d\r\n", width);
  ret = get_mbr();
  if(ret != ESP_OK) {    ESP_LOGI("", "get_mbr : %s\r\n", esp_err_to_name(ret));  }
  ret = get_bpb();
  if(ret != ESP_OK) {    ESP_LOGI("", "get_bpb : %s\r\n", esp_err_to_name(ret));  }


/*
 * 
 *  sdmmc_host_set_cclk_always_on(int slot, bool cclk_always_on);
 *  sdmmc_host_set_bus_width(int slot, size_t width);
 *  sdmmc_host_set_card_clk(int slot, uint32_t freq_khz);
 *  sdmmc_host_set_bus_ddr_mode(int slot, bool ddr_enabled);
 *  sdmmc_host_get_real_freq(int slot, int *real_freq_khz);
 *  sdmmc_host_set_input_delay(int slot, sdmmc_delay_phase_t delay_phase);
 *  
 *  SDMMC_SLOT_FLAG_INTERNAL_PULLUP macro
 *  
 */
  
}

void SDMMC_FAT32::end() {
  ret = sdmmc_host_deinit() ;
  if(ret != ESP_OK){
    ESP_LOGI("", "sdmmc_host_deinit : %s\r\n", esp_err_to_name(ret));
  }
}

esp_err_t SDMMC_FAT32::get_mbr() {
  ret = read_block(&mbrStruct, 0, 1);
  if (ret == ESP_OK) {
    for (int i = 0; i < 4; i++) {
      if (mbrStruct.partitionData[i].fsType == 11  ||  mbrStruct.partitionData[i].fsType == 12) { 
        _firstSector  = mbrStruct.partitionData[i].firstSector;
        ESP_LOGI("","_firstSector %d\r\n", _firstSector);
        _fsType       = mbrStruct.partitionData[i].fsType;
        ESP_LOGI("","_fsType %d\r\n", _fsType);
        _partitionId = i ;        
        _sectorsTotal  = mbrStruct.partitionData[i].sectorsTotal;
        ESP_LOGI("","_sectorsTotal %d\r\n", _sectorsTotal);
        ESP_LOGI("","Partition size = %d MB\r\n" , _sectorsTotal / 1024 / 1024 * BYTES_PER_SECTOR );
        ESP_LOGI("",">>>Reading MBR done");
        ret = ESP_OK;
        break;
      } else {
        ret = ESP_ERR_NOT_SUPPORTED;
      }
    }
  }
  return ret;
}

esp_err_t SDMMC_FAT32::get_bpb() {
  ret = read_block(&bpbStruct, mbrStruct.partitionData[0].firstSector, 1);
  if (ret == ESP_OK) {
    _sectorsPerFat      = bpbStruct.sectorsPerFat;
    ESP_LOGI("","_sectorsPerFat %d\r\n", _sectorsPerFat);
    _bytesPerSector     = bpbStruct.bytesPerSector;
    ESP_LOGI("","_bytesPerSector %d\r\n", _bytesPerSector);
    _numFats            = bpbStruct.numberofFATs;
    ESP_LOGI("","_numFats %d\r\n", _numFats);
    _reservedSectors    = bpbStruct.reservedSectorCount;
    ESP_LOGI("","_reservedSectors %d\r\n", _reservedSectors);    
    _rootCluster    = bpbStruct.rootCluster;
    ESP_LOGI("","_rootCluster %d\r\n", _rootCluster);
    _firstDataSector    = _sectorsPerFat * _numFats + _reservedSectors ;
    ESP_LOGI("","_firstDataSector %d\r\n", _firstDataSector);
    _sectorsPerCluster  = bpbStruct.sectorPerCluster;
    ESP_LOGI("","_sectorsPerCluster %d\r\n", _sectorsPerCluster);
    _bytesPerCluster  = _sectorsPerCluster * _bytesPerSector ;
    ESP_LOGI("","_bytesPerCluster %d\r\n", _bytesPerCluster);
    ESP_LOGI("",">>>Reading BPB done.");
  } else {
    ESP_LOGI("","SDMMC: ERROR reading partition data");
    _sectorsPerFat      = 1;
    _sectorsPerCluster = 1;
    _bytesPerCluster = 1;
    _bytesPerSector = 512;
  }
  return ret;
}

uint32_t SDMMC_FAT32::getNextSector(uint32_t curSector) {
  if (curSector == 0) {
    curSector = _sectorInBuf;
    ESP_LOGI("","curSector = 0");
  }
  uint32_t curCluster = clusterBySector(curSector);
  uint32_t nextSector = curSector + 1;
  uint32_t nextCluster = clusterBySector(nextSector);
  if (curCluster != nextCluster) {
    nextCluster = getNextCluster(curCluster);
    if (fat_entry_type(nextCluster)==LAST_CLUSTER) {
      return 0;
    }
    nextSector = firstSectorOfCluster(nextCluster);
  }
  return nextSector;
}

uint32_t SDMMC_FAT32::getNextCluster(uint32_t cluster) {
  uint32_t nextCluster;
  uint32_t neededSector = fatSectorByCluster(cluster);
  uint32_t recOffset = fatByteOffset(cluster) / 4;
  if (neededSector < _firstCachedFatSector || neededSector > _lastCachedFatSector) {
    cache_fat(neededSector);
  }
  nextCluster = fat_cache.uint32[(neededSector - _firstCachedFatSector) * (BYTES_PER_SECTOR/4) + recOffset];  
  return nextCluster;
}


entry_t* SDMMC_FAT32::findEntry(const fpath_t& search_path) {
  entry_t* entry;
  fpath_t name, testname;
  name = search_path;
  name.toUpperCase();
  name.replace("\\",""); 
  ESP_LOGI("","Searching in <%s> for [%s] entry:\r\n", _currentDir.c_str(), search_path.c_str());
  rewindDir();
  entry = nextEntry();
  while (!entry->is_end) {
    testname = entry->name;
    testname.toUpperCase();
    //ESP_LOGI("","compare <%s> <%s>\r\n", name.c_str(), testname.c_str());
    if (testname == name) {
      return entry;
      break;
    }
    entry = nextEntry();
  }
  entry->is_dir = -1;
  entry->is_end = 1;
  entry->name = "";
  entry->size = 0;
  return entry;
}

uint32_t SDMMC_FAT32::findEntryCluster(const fpath_t& search_path) {
  uint32_t first_cluster = 0;
  entry_t* entry;
  fpath_t name, testname;
  name = search_path;
  name.toUpperCase();
  name.replace("\\",""); 
  ESP_LOGI("","Searching in <%s> for [%s] entry cluster:\r\n", _currentDir.c_str(), search_path.c_str());
  rewindDir();
  entry = nextEntry();
  while (!entry->is_end) {
    testname = entry->name;
    testname.toUpperCase();
    //ESP_LOGI("","compare <%s> <%s>\r\n", name.c_str(), testname.c_str());
    if (testname == name) {
      first_cluster = clusterBySector(entry->sectors[0].first);
      break;
    }
    entry = nextEntry();
  }
  return first_cluster;
}

uint8_t* SDMMC_FAT32::readSector(uint32_t sec) {
  read_sector(sec);
  return &sector_buf[0];
}

uint8_t* SDMMC_FAT32::readFirstSector(const fname_t& fname) {
  uint32_t sec = findEntrySector(fname);
  read_sector(sec);
  return &sector_buf[0];
}

uint8_t* SDMMC_FAT32::readFirstSector(entry_t* entry) {
  uint32_t sec = entry->sectors[0].first;
  read_sector(sec);
  return &sector_buf[0];
}

uint8_t* SDMMC_FAT32::readNextSector(uint32_t curSector) {
  uint32_t sec = getNextSector(curSector);
  if (sec > 0) {
    read_sector(sec);
    return &sector_buf[0];
  } else {
    return (uint8_t*)nullptr;
  }
}

void SDMMC_FAT32::setCurrentDir(fpath_t dir_path){
  // not fully implemented, for now it accepts "" or "/" as a root folder, or some name without slashes as a directory in a root, yes only 1 level
  if (dir_path == "" || dir_path == "/" || dir_path == "\\") {
    // root
    _currentDir = "/";
    _startCluster   = _rootCluster;
  } else {
    dir_path.replace("\\","/"); // unix style
    dir_path.replace("//","/"); // in case of some confuses
    // now we only get the last part of the path supplied
    if (dir_path.endsWith("/")) {
      dir_path.remove(dir_path.length() - 1, 1);
    }
    int i = dir_path.lastIndexOf("/");
    if (i >= 0) {
      dir_path.remove(0, i + 1);
    }
    // here we are supposed to have the last portion of the given pathname
    _startCluster   = findEntryCluster(dir_path);
    _currentDir = dir_path;
  }
  _currentCluster   = _startCluster;
  _startSector      = firstSectorOfCluster(_startCluster);
  _currentSector    = _startSector;
  ESP_LOGI("","SDMMC: Current ROOT set to <%s>\r\n", _currentDir.c_str() );
}


void SDMMC_FAT32::setCurrentPoint(point_t& p) {
  cache_dir(p.firstCachedDirSector);
  cache_fat(p.firstCachedFatSector);
  _currentEntry   = p.entry;
  _currentDir     = p.currentDir;
  _startSector    = p.startSector;
  _startCluster   = p.startCluster;
  _currentSector  = p.curSector;
  _currentCluster = p.curCluster;
  _dirent_num     = p.direntNum;
}


point_t SDMMC_FAT32::getCurrentPoint() {
  point_t p;
  entry_t ent;
  p.entry       = _currentEntry;
  p.currentDir  = _currentDir;
  p.startSector = _startSector;
  p.startCluster= _startCluster;
  p.curSector   = _currentSector;
  p.curCluster  = _currentCluster;
  p.firstCachedFatSector = _firstCachedFatSector;
  p.firstCachedDirSector = _firstCachedDirSector;
  p.direntNum   = _dirent_num;
  return p;
}


void SDMMC_FAT32::rewindDir() {
  _currentCluster       = _startCluster;
  _startSector          = firstSectorOfCluster(_startCluster);
  _currentSector        = _startSector;
  _dirent_num           = 0;
  _currentEntry.name    = "";
  _currentEntry.size    = 0;
  _currentEntry.is_dir  = -1;
  _currentEntry.sectors.clear();
}

entry_t* SDMMC_FAT32::nextEntry() {
  entry_t* ent;
  ent = buildNextEntry();
  while (!(ent->is_end)) {
    if (ent->is_dir>=0)
    {
  //    ESP_LOGI("","inside nextEntry() %s %d\r\n", ent->name.c_str(), ent->sectors[0].first); 
      return ent;
    }
    ent = buildNextEntry();
  }
  return ent;
}

entry_t* SDMMC_FAT32::buildNextEntry() {
  uint32_t cl, cl_addr;
  uint8_t fname[513];
  memset(&fname, 0, 513);
  uint8_t attr, n;
  fname_t filename, shortname;
  chain_t chain;
  esp_err_t ret;
  bool entry_done = false;
  _currentEntry.name = "";
  _currentEntry.size = 0;
  _currentEntry.is_dir = -1;
  _currentEntry.is_end = 0;
  _currentEntry.sectors.clear();
  
  while (!entry_done) {
    if (_dirent_num >= NDIR_PER_SEC) { // going out of sector
      _dirent_num = 0;
      if (clusterBySector(_currentSector+1) != _currentCluster) { // going out of cluster
        _currentCluster = getNextCluster(_currentCluster);
        if (fat_entry_type(_currentCluster)==LAST_CLUSTER) {
          rewindDir();
          _currentEntry.is_end = 1;
          break;
        }
        _currentSector = firstSectorOfCluster(_currentCluster);
      } else {
        _currentSector++;
      }
    }

    if (_currentSector==_sectorInBuf) {
      ret = ESP_OK; // already buffered
    } else {
      ret = read_sector(_currentSector);
      if (ret!=ESP_OK) break;
    } 

    sfn_dir_t (&rec_array)[NDIR_PER_SEC] = reinterpret_cast<sfn_dir_t(&)[NDIR_PER_SEC]>(sector_buf);

    attr = rec_array[_dirent_num].attr;
    if (attr == FAT32_LONG_FILE_NAME) {     // process LFN entry part
      entry_done = false;
      lfn_dir_t (&lfn_entry) = reinterpret_cast<lfn_dir_t&>(rec_array[_dirent_num]);
      // lfn_print_ent(&lfn_entry, lfn_entry.cksum);
      n = lfn_entry.seqno;
      // helpers: lfn_is_first(n), lfn_is_last(n), lfn_is_deleted(n)
      if ( lfn_is_deleted(n) ) {
//        ESP_LOGI("","\tSkipping deleted LFN entry");
        _dirent_num++;
        continue;
      }
      n = (uint8_t)((n)<<2)>>2;
      n = (n-1)*26 ; // strip 6th bit and bring it to zero base , multiply by 26 (lfn part is 26 bytes long)
      memcpy(&(fname[n]),  &(lfn_entry.name1_5),  10);
      memcpy(&(fname[10+n]), &(lfn_entry.name6_11), 12);
      memcpy(&(fname[22+n]), &(lfn_entry.name12_13), 4);

    } else {                                // rec_array[i] contains SFN entry or empty record
      // dirent_print_helper(&(rec_array[i]));
      entry_done = true;
      if (rec_array[_dirent_num].attr&FAT32_VOLUME_LABEL || rec_array[_dirent_num].attr&FAT32_HIDDEN || rec_array[_dirent_num].attr&FAT32_SYSTEM_FILE || dirent_free(&(rec_array[_dirent_num]))) {
        memset(&fname, 0, 513);
        _dirent_num++;
        continue; // skip deleted, empty, system and hidden, sysvol
      }
      shortname = to_8dot3(rec_array[_dirent_num].filename);
//      ESP_LOGI("",">>>\tShort name = <%s>\r\n", shortname.c_str());
      
    }
    if (entry_done) {
      filename = unicode2ascii(reinterpret_cast<uint16_t*>(&fname), MAX_NAME_LEN)  ;
//      ESP_LOGI("",">>>\tLong name = <%s>\r\n\r\n", filename.c_str());
      if (shortname>"" && filename == "") filename = shortname;
      if (filename.endsWith(".")) filename.remove(filename.length()-1,1);
      _currentEntry.name = filename;
      _currentEntry.size =  rec_array[_dirent_num].file_nbytes;
      _currentEntry.is_dir = (rec_array[_dirent_num].attr&FAT32_DIR);

      cl = fat32_cluster_id(&rec_array[_dirent_num]);
      
      chain.first=firstSectorOfCluster(cl);
      chain.last = chain.first;
      cl_addr = cl;
      while (true) {
        cl = getNextCluster(cl_addr);
        if (fat_entry_type(cl)==LAST_CLUSTER) {
          chain.last = lastSectorOfCluster(cl_addr);
          break;
        }
        if ( (cl - cl_addr) != 1 ) { 
          chain.last = lastSectorOfCluster(cl_addr);
          _currentEntry.sectors.push_back(chain);
          chain.first = firstSectorOfCluster(cl);
        }
        cl_addr = cl;
      }
      _currentEntry.sectors.push_back(chain);

    //  ESP_LOGI("","First:");
    //  ESP_LOGI("",chain.first);
    //  ESP_LOGI("","Last:");
     // ESP_LOGI("",chain.last);            
      memset(&fname, 0, 513);
      _dirent_num++;
      return &_currentEntry;
    } else {
      _dirent_num++;
    }
  }
  return &_currentEntry;
}

void SDMMC_FAT32::printCurrentDir() {
  ESP_LOGI("","Directory <%s>:\r\n", _currentDir.c_str());
  const char cdir[] = {"<DIR>"};
  const char cfile[] = {""};
  rewindDir();
  entry_t* ent;
  int i = 0;
  ent = nextEntry();
  while (!(ent->is_end)) {
    if (ent->size >= 10240) {
      ESP_LOGI("","%d:\t%s\t%s\t%d kB\r\n", i, ent->is_dir ? cdir : cfile, ent->name.c_str(), ent->size/1024);
    } else {
      ESP_LOGI("","%d:\t%s\t%s\t%d Bytes\r\n", i, ent->is_dir ? cdir : cfile, ent->name.c_str(), ent->size);
    }
    ent = nextEntry();
    i++;
  }
}

int SDMMC_FAT32::fat_entry_type(uint32_t x) {
    // eliminate upper 4 bits: error to not do this.
    x = (x << 4) >> 4;
    switch(x) {
      case FREE_CLUSTER:
      case RESERVED_CLUSTER:
      case BAD_CLUSTER:
        return x;
    }
    if(x >= 0x2 && x <= 0xFFFFFEF)
        return USED_CLUSTER;
    if(x >= 0xFFFFFF0 && x <= 0xFFFFFF6)
    //    ESP_LOGI("","reserved value: %x\n", x);
    if(x >=  0xFFFFFF8  && x <= 0xFFFFFFF)
        return LAST_CLUSTER;
    // ESP_LOGI("","impossible type value: %x\n", x);
    return LAST_CLUSTER; //???
}

int SDMMC_FAT32::dirent_free(sfn_dir_t *d) {
    uint8_t x = d->filename[0];
    if(d->attr == FAT32_LONG_FILE_NAME)
        return (dirent_is_deleted_lfn(d));
    return x == 0 || x == 0xE5;
}

// trim spaces from the first 8.
// not re-entrant, gross.
const char* SDMMC_FAT32::to_8dot3(const char p[11]) {
    static char s[14];
    const char *suffix = &p[8];
    // find last non-space
    memcpy(s, p, 8);
    int i = 7; 
    for(; i >= 0; i--) {
      if(s[i] != ' ') {
        i++;
        break;
      }
    }
    s[i++] = '.';
    for(int j = 0; j < 3; j++) {
      if (suffix[j] !=' ') { 
        s[i++] = suffix[j];
      }
    }
    s[i++] = 0;
    return s;
}

void SDMMC_FAT32::dirent_name(sfn_dir_t *d,  char *fname) {
    strcpy(fname, to_8dot3(d->filename));
}

// not re-entrant, gross.
const char* SDMMC_FAT32::add_nul(const char filename[11]) {
    static char s[14];
    memcpy(s, filename, 11);
    s[11] = 0;
    return s;
}

uint8_t SDMMC_FAT32::lfn_checksum(const char* pFCBName) {
  uint8_t sum = 0;
  for (int i = 11; i; i--)
    sum = ((sum & 1) << 7) + (sum >> 1) + *pFCBName++;
  return sum;
}

int SDMMC_FAT32::lfn_is_deleted(uint8_t seqno) {
  return (seqno & 0xe5) == 0xe5;
}

int SDMMC_FAT32::dirent_is_deleted_lfn(sfn_dir_t *d) {
  assert(d->attr == FAT32_LONG_FILE_NAME);
  lfn_dir_t *l = reinterpret_cast<lfn_dir_t*>(d);
  return lfn_is_deleted(l->seqno);
}

void SDMMC_FAT32::dirent_print_helper(sfn_dir_t *d) {
    ESP_LOGI("",">>>\t");
    if(dirent_free(d))  {
        ESP_LOGI("","dirent is not allocated\n");
        return;
    }
    if(d->attr == FAT32_LONG_FILE_NAME) {
        ESP_LOGI("","dirent is an LFN\n");
        return;
    }
    if(is_attr(d->attr, FAT32_ARCHIVE)) {
        ESP_LOGI("","[ARCHIVE]: assuming short part of LFN:");
    } else if(!(is_attr(d->attr, FAT32_DIR))) {
        ESP_LOGI("","need to handle attr %x \n", d->attr);
        if(is_attr(d->attr, FAT32_ARCHIVE))
            return;
    }
    ESP_LOGI("","\tfilename      = raw=<%s> 8.3=<%s>\n", add_nul(d->filename),to_8dot3(d->filename));
    ESP_LOGI("","\tbyte version  = {");
    for(int i = 0; i < sizeof d->filename; i++) {
        if(i==8) {
            ESP_LOGI("","\n");
            ESP_LOGI("","\t\t\t\t");
        }
        ESP_LOGI("","[%c]/%x,", d->filename[i],d->filename[i]);
    }
    ESP_LOGI("","}");
        
    ESP_LOGI("","\tattr         = %x ", d->attr);
    if(d->attr != FAT32_LONG_FILE_NAME) {
        if(d->attr&FAT32_RO)
            ESP_LOGI(""," [Read-only]\n");
        if(d->attr&FAT32_HIDDEN)
            ESP_LOGI(""," [HIDDEN]\n");
        if(d->attr&FAT32_SYSTEM_FILE)
            ESP_LOGI(""," [SYSTEM FILE: don't move]\n");
        ESP_LOGI("","");
    }
 
    ESP_LOGI("","\thi_start      = %x\n", d->hi_start);
    ESP_LOGI("","\tlo_start      = %d\n", d->lo_start);
    ESP_LOGI("","\tfile_nbytes   = %d\n", d->file_nbytes);
}

void SDMMC_FAT32::lfn_print_ent(lfn_dir_t *l, uint8_t cksum) {
    uint8_t n = l->seqno;
    fname_t t;
    ESP_LOGI("",">>>\tseqno = %x, deleted=%d\n", n, lfn_is_deleted(n));
    uint8_t buf[27];
    memcpy(&buf[0],  l->name1_5,  10);
    memcpy(&buf[10], l->name6_11, 12);
    memcpy(&buf[22], l->name12_13, 4);
    buf[26] = 0;
    
    for(int i = 0; i < 26; i += 2) {
        if(buf[i] == 0 && buf[i+1] == 0)
            break;
        ESP_LOGI("","\t\tlfn[%d] = [%c] = %x\n", i, buf[i], buf[i]);
    }
    ESP_LOGI("","\tcksum=%x (expected=%x)\n", l->cksum, cksum);
}


fname_t SDMMC_FAT32::unicode2ascii(uint16_t* in, int len) {
  uint16_t x;
  fname_t out = "";
  for(int i = 0; i < len ; i++) {
    x = in[i];
    if (x==0xffff) x = 0;
    x = (uint16_t)(x << 9)>>9;
    if (x<32 && x>0) x = 94;
    switch (x) {
      case 34:
      case 42:
      case 47:
      case 58:
      case 60:
      case 62:
      case 63:
      case 92:
      case 124:
      case 127:
        x = 94;
        break;
    }
    if (x==0) continue;
    out += (char)x;
  }
  return out;
}
