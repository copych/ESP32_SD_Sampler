#pragma once

/*
 * Simplified READ-ONLY FAT32 class for fast accessing and reading of WAV files.
 * Cluster chains are pre-cached for the current directory files, and stored in memory as 
 * a collection of linear sector chains boundaries. Ideally, when no fragmentation appears,
 * only a single pair of sectors (beginning/end) per file needed.
 */
/*
Just for your information
Of what I have tested with some random 16GB card:

sectors per read  |  reading speed, MB/s
------------------+---------------------
        1         |        0.90
        2         |        1.57
        4         |        2.82
        8         |        5.00
       16         |        7.79
       32         |       11.12
       64         |       13.85
      128         |       15.75

 * SPI access is SIGNIFICANTLY SLOWER than SD_MMC (like 1/3 of SD_MMC speed), 
 * so 4-bit MMC is the only possible variant for playing back samples from SD_MMC.
 * Arduino ESP32 core libs are able of using 4-bit bus, and it's a good thing.
 * Thus D1 and D2 are mandatory to have a good speed
 * Be aware that the vast majority of micro-SD breakout boards are produced
 * with D1 and D2 NOT CONNECTED
 *
 * pin 1 - D2                |  Micro SD card     |
 * pin 2 - D3                |                   /
 * pin 3 - CMD               |                  |__
 * pin 4 - VDD (3.3V)        |                    |
 * pin 5 - CLK               | 8 7 6 5 4 3 2 1   /
 * pin 6 - VSS (GND)         | - - - - - - - -  /
 * pin 7 - D0                | - - - - - - - - |
 * pin 8 - D1                |_________________|
 *                             ¦ ¦ ¦ ¦ ¦ ¦ ¦ ¦
 *                     г=======- ¦ ¦ ¦ ¦ ¦ ¦ L=========¬
 *                     ¦         ¦ ¦ ¦ ¦ ¦ L======¬    ¦
 *                     ¦   г=====- ¦ ¦ ¦ L=====¬  ¦    ¦
 * Connections for     ¦   ¦   г===¦=¦=¦===¬   ¦  ¦    ¦
 * full-sized          ¦   ¦   ¦   г=- ¦   ¦   ¦  ¦    ¦
 * SD card             ¦   ¦   ¦   ¦   ¦   ¦   ¦  ¦    ¦
 * ESP32-P4 DevKit  | 40  39  GND  43 3V3 GND  44 42  41  |
 * ESP32-S3 DevKit  | 21  47  GND  39 3V3 GND  40 41  42  |
 * ESP32-S3-USB-OTG | 38  37  GND  36 3V3 GND  35 34  33  |
 * ESP32            |  4   2  GND  14 3V3 GND  15 13  12  |
 * Pin name         | D1  D0  VSS CLK VDD VSS CMD D3  D2  |
 * SD pin number    |  8   7   6   5   4   3   2   1   9 /
 *                  |                                  =/
 *                  |__=___=___=___=___=___=___=___=___/
 * WARNING: ALL data pins must be pulled up to 3.3V with an external 10k Ohm resistor!
 * Note to ESP32 pin 2 (D0): Add a 1K Ohm pull-up resistor to 3.3V after flashing
 *
 * SD Card | ESP32
 *    D2       12
 *    D3       13
 *    CMD      15
 *    VSS      GND
 *    VDD      3.3V
 *    CLK      14
 *    VSS      GND
 *    D0       2  (add 1K pull up after flashing)
 *    D1       4
 *
 *    For more info see file README.md in this library or on URL:
 *    https://github.com/espressif/arduino-esp32/tree/master/libraries/SD_MMC
 *    
 *  
 */
#include "sdmmc_types.h"
 


/* 
 * Simplified read-only SD_MMC FAT32 (for wav sampler)
 */

class SDMMC_FAT32 {  
  public:
    SDMMC_FAT32() {} ;
    ~SDMMC_FAT32() {} ;
   
    esp_err_t ret;
    sdmmc_card_t card;

    void begin();
    void end();
    void testReadSpeed(uint32_t sectorsPerRead, uint32_t totalMB);
    void setCurrentDir(fpath_t pathToDir);
    void printCurrentDir();
    void rewindDir();
    entry_t*  findEntry(const fpath_t& fname);
    entry_t*  nextEntry();
    entry_t*  buildNextEntry();
    entry_t*  getCurrentEntryP()            {return &_currentEntry;};
    entry_t   getCurrentEntry()             {return _currentEntry;};
    void      setCurrentEntry(entry_t ent)  {_currentEntry = ent;};
 
    esp_err_t read_block(void *dst, uint32_t start_sector, uint32_t sector_count);
    esp_err_t read_sector(uint32_t sector); // reads one sector into uint8_t sector_buf[]
    esp_err_t cache_fat( uint32_t sector ); // reads FAT_CACHE_SECTORS into uint8_t fat_cache.uint8[] sets first and last sectors read
    esp_err_t cache_dir( uint32_t sector ); // reads DIR_CACHE_SECTORS into uint8_t dir_cache[] sets first and last sectors read
    esp_err_t write_block(const void *source, uint32_t block, uint32_t size);
    uint8_t*  readFirstSector(const fname_t& fname); // 
    uint8_t*  readFirstSector(entry_t* entry);
    uint8_t*  readSector(uint32_t sector); // reads one sector into uint8_t sector_buf[] returns pointer to it
    uint8_t*  readNextSector(uint32_t curSector);
    uint32_t  getNextSector(uint32_t curSector = 0);
    
    point_t   getCurrentPoint()     ;
    void      setCurrentPoint(point_t& p);

    uint8_t   getPartitionId()      {return _partitionId;}
    uint32_t  getFirstSector()      {return _firstSector;}
    uint32_t  getFsType()           {return _fsType;}
    uint32_t  getFirstDataSector()  {return _firstDataSector;}
    uint8_t   getNumFats()          {return _numFats;}
    uint32_t  getReservedSectors()  {return _reservedSectors;}
    uint32_t  getSectorsPerFat()    {return _sectorsPerFat;}
    uint32_t  getSectorsPerCluster(){return _sectorsPerCluster;}
    uint32_t  getBytesPerSector()   {return _bytesPerSector;}
    uint32_t  getBytesPerCluster()  {return _bytesPerCluster;}
    
    esp_err_t get_mbr();
    esp_err_t get_bpb(); 
    
  private: 
    struct __attribute__((packed)) {
      uint8_t   code[440];
      uint32_t  diskSerial;   // This is optional
      uint16_t  reserved;     // Usually 0x0000
      part_t    partitionData[4];
      uint8_t   bootSign[2];  // 0x55 0xAA for bootable
    } mbrStruct;

    struct __attribute__((packed)) {
      uint8_t   jumpBoot[3]; 
      uint8_t   OEMName[8]; 
      uint16_t  bytesPerSector; 
      uint8_t   sectorPerCluster; 
      uint16_t  reservedSectorCount;
      uint8_t   numberofFATs;
      uint16_t  rootEntryCount; 
      uint16_t  totalSectors_F16;   //  [19, 20]
      uint8_t   mediaType;          //  [21]
      uint16_t  FATsize_F16;        //  [22, 23]
      uint16_t  sectorsPerTrack;    //  [24, 25]
      uint16_t  numberofHeads;      //  [26, 27]
      uint32_t  hiddenSectors;      //  [28, 31]
      uint32_t  totalSectors;       //  [32, 35]
      uint32_t  sectorsPerFat;      //  [36, 39]
      uint16_t  extFlags;           //  [40, 41]
      uint16_t  FSversion;          //  [42, 43]
      uint32_t  rootCluster; 
      uint16_t  FSinfo;             //  [48, 49]
      uint16_t  BackupBootSector;   //  [50, 51]
      uint8_t   reserved[12]; 
      uint8_t   driveNumber; 
      uint8_t   reserved1; 
      uint8_t   bootSignature; 
      uint32_t  volumeID; 
      uint8_t   volumeLabel[11]; 
      uint8_t   fileSystemType[8]; 
      uint8_t   bootData[420];
      uint16_t  bootEndSignature; 
    } bpbStruct ;

    uint8_t sector_buf[BYTES_PER_SECTOR]  ; // read_sector(sector) uses this buf
    volatile uint32_t  _sectorInBuf = 0;
    volatile int       _dirent_num = 0;

    uint8_t dir_cache[BYTES_PER_SECTOR * DIR_CACHE_SECTORS]  ; // cache_dir()

    union {
      uint32_t uint32[BYTES_PER_SECTOR * FAT_CACHE_SECTORS / 4];
      uint8_t  uint8[BYTES_PER_SECTOR * FAT_CACHE_SECTORS];
    } fat_cache;
    uint8_t   _partitionId;
    uint32_t  _firstSector;
    uint32_t  _fsType;
    uint32_t  _firstDataSector;
    uint8_t   _numFats;
    uint32_t  _reservedSectors;
    uint32_t  _sectorsPerFat;
    uint32_t  _bytesPerSector=1;
    uint32_t  _sectorsPerCluster=1;
    uint32_t  _rootCluster;
    uint32_t  _sectorsTotal;
    volatile uint32_t  _firstCachedFatSector = 0;
    volatile uint32_t  _lastCachedFatSector = 0;
    volatile uint32_t  _firstCachedDirSector = 0;
    volatile uint32_t  _lastCachedDirSector = 0;
    volatile uint32_t  _currentSector;
    volatile uint32_t  _currentCluster;
    fpath_t   _currentDir;
    entry_t   _currentEntry;
    uint32_t  _bytesPerCluster;
    volatile uint32_t  _startSector;
    volatile uint32_t  _startCluster;
    /*
     * Cluster and sector calculations
     */
    inline uint32_t fatSectorByCluster(uint32_t cluster)    {return _firstSector + cluster * 4 / _bytesPerSector + _reservedSectors; }
    inline uint32_t fatClusterBySector(uint32_t sector)     {return (sector - _reservedSectors  - _firstSector) * _bytesPerSector / 4 ; }
    inline uint32_t fatByteOffset(uint32_t cluster)         {return cluster * 4 % _bytesPerSector; }
    inline uint32_t firstSectorOfCluster(uint32_t cluster)  {return _firstSector + (cluster - _rootCluster) * _sectorsPerCluster + _firstDataSector ; }
    inline uint32_t lastSectorOfCluster(uint32_t cluster)   {return firstSectorOfCluster(cluster + 1) - 1 ; }
    inline uint32_t clusterBySector(uint32_t sector)        {return (sector - _firstSector  - _firstDataSector) / _sectorsPerCluster + _rootCluster ; }
    inline uint32_t fat32_cluster_id(uint16_t high, uint16_t low)  {return high << 16 | low; }
    inline uint32_t fat32_cluster_id(sfn_dir_t *d)   {return d->hi_start << 16 | d->lo_start; }
    uint32_t        getNextCluster(uint32_t cluster);
    uint32_t        findEntryCluster(const fpath_t& search_name) ; // returns first sector of a file
    uint32_t        findEntrySector(const fpath_t& search_name) {return firstSectorOfCluster(findEntryCluster(search_name));}

    /**********************************************************************
     * fat32 helpers.
     */
    
    // convert name from 8.3
    void dirent_name(sfn_dir_t *d,  char *name);
    
    inline int is_dir(sfn_dir_t *d) { return d->attr == FAT32_DIR; }
    inline int dirent_is_lfn(sfn_dir_t *d) { return (d->attr == FAT32_LONG_FILE_NAME); }
    
    inline int is_attr(uint32_t x, uint32_t flag) { 
        if(x == FAT32_LONG_FILE_NAME)
            return x == flag;
        return (x & flag) == flag;
    }
    void dirent_print_helper(sfn_dir_t *d);
    int dirent_free(sfn_dir_t *d);
    int dirent_is_deleted_lfn(sfn_dir_t *d);
    int fat_entry_type(uint32_t x);
    const char* fat_entry_type_str(uint32_t x);
    
    const char* to_8dot3(const char*);
    const char* add_nul(const char*);
        
   // sfn_dir_t* dir_filename(char*, sfn_dir_t*, sfn_dir_t*);
    
    int dir_lookup(const char *name, sfn_dir_t *dirs, unsigned n);
    uint8_t lfn_checksum(const char*);
    int lfn_is_deleted(uint8_t);
    void lfn_print_ent(lfn_dir_t*, uint8_t);
    int lfn_terminator(uint8_t*);
    void lfn_print(lfn_dir_t*, int, uint8_t, int); 
    fname_t unicode2ascii(uint16_t*, int len);

};
