/*
 * reverb stuff
 *
 * This is a port of the public code from YetAnotherElectronicsChannel
 * Based on main.c I've created this module
 *
 * src: https://github.com/YetAnotherElectronicsChannel/STM32_DSP_Reverb/blob/master/code/Src/main.c
 *
 * The explanation of the original module can be found here: https://youtu.be/nRLXNmLmHqM
 *
 * Changes:
 * - optimized for buffer processing
 * - added interface to set the level
 *
 */ 

#ifdef BOARD_HAS_PSRAM 
  #define REV_MULTIPLIER 1.8f
  #define MALLOC_CAP        MALLOC_CAP_SPIRAM
#else
  #define REV_MULTIPLIER 0.35f
  #define MALLOC_CAP        MALLOC_CAP_INTERNAL
#endif

#define COMB_BUF_LEN_0 (int)( 3604.0 * REV_MULTIPLIER)
#define COMB_BUF_LEN_1 (int)( 3112.0 * REV_MULTIPLIER)
#define COMB_BUF_LEN_2 (int)( 4044.0 * REV_MULTIPLIER)
#define COMB_BUF_LEN_3 (int)( 4492.0 * REV_MULTIPLIER)
#define ALLPASS_BUF_LEN_0 (int)( 500.0 * REV_MULTIPLIER)
#define ALLPASS_BUF_LEN_1 (int)( 168.0 * REV_MULTIPLIER)
#define ALLPASS_BUF_LEN_2 (int)( 48.0 * REV_MULTIPLIER)

//rev_time 0.0 <-> 1.0
//rev_delay 0.0 <-> 1.0

class FxReverb {
	public:
	FxReverb() {
  
  }

  	inline void Process( float *signal_l, float *signal_r ){
  
  		float inSample;
  
  		// create mono sample 
  		inSample = *signal_l + *signal_r; // it may cause unwanted audible effects 
  		inSample *= 0.5f;
  
  		// float newsample = (Do_Comb0(inSample) + Do_Comb1(inSample) + Do_Comb2(inSample) + Do_Comb3(inSample)) / 4.0f;
  		float newsample = ((float)Do_Comb0(inSample) + (float)Do_Comb1(inSample) + (float)Do_Comb2(inSample) + (float)Do_Comb3(inSample)) * 0.25f;
  		newsample = Do_Allpass0(newsample);
  		newsample = Do_Allpass1(newsample);
  		newsample = Do_Allpass2(newsample);
  
  		// apply reverb level 
  		newsample *= rev_level;
  
  		*signal_l += newsample;
  		*signal_r += newsample;
  
  	}
  
  	inline void Init() { 

        combBuf0 = (float*)heap_caps_aligned_alloc( BYTE_ALIGN sizeof(float) * COMB_BUF_LEN_0 , MALLOC_CAP );
        if( combBuf0 == NULL){
          ESP_LOGI("","No more RAM for reverb combBuf0!");
        } else {
          ESP_LOGI("","REVERB: combBuf0 : ");
          ESP_LOGI("","%d Bytes RAM allocated for reverb buffer, &=%#010x\r\n", sizeof(float) * COMB_BUF_LEN_0 , combBuf0);
          memset(combBuf0, 0, sizeof(float) * COMB_BUF_LEN_0);
        } 
        combBuf1 = (float*)heap_caps_aligned_alloc( BYTE_ALIGN sizeof(float) * COMB_BUF_LEN_1 , MALLOC_CAP );
        if( combBuf1 == NULL){
          ESP_LOGI("","No more RAM for reverb combBuf1!");
        } else {
          ESP_LOGI("","REVERB: combBuf1 : ");
          ESP_LOGI("","%d Bytes RAM allocated for reverb buffer, &=%#010x\r\n", sizeof(float) * COMB_BUF_LEN_1 , combBuf1);
          memset(combBuf1, 0, sizeof(float) * COMB_BUF_LEN_1);
        }
        combBuf2 = (float*)heap_caps_aligned_alloc( BYTE_ALIGN sizeof(float) * COMB_BUF_LEN_2 , MALLOC_CAP );
        if( combBuf2 == NULL){
          ESP_LOGI("","No more RAM for reverb combBuf2!");
        } else {
          ESP_LOGI("","REVERB: combBuf2 : ");
          ESP_LOGI("","%d Bytes RAM allocated for reverb buffer, &=%#010x\r\n", sizeof(float) * COMB_BUF_LEN_2 , combBuf2);
          memset(combBuf2, 0, sizeof(float) * COMB_BUF_LEN_2);
        }
        combBuf3 = (float*)heap_caps_aligned_alloc( BYTE_ALIGN sizeof(float) * COMB_BUF_LEN_3 , MALLOC_CAP );
        if( combBuf3 == NULL){
          ESP_LOGI("","No more RAM for reverb combBuf2!");
        } else {
          ESP_LOGI("","REVERB: combBuf3 : ");
          ESP_LOGI("","%d Bytes RAM allocated for reverb buffer, &=%#010x\r\n", sizeof(float) * COMB_BUF_LEN_3 , combBuf3);
          memset(combBuf3, 0, sizeof(float) * COMB_BUF_LEN_3);
        } 
        allPassBuf0 = (float*)heap_caps_aligned_alloc( BYTE_ALIGN sizeof(float) * ALLPASS_BUF_LEN_0 , MALLOC_CAP );
        if( allPassBuf0 == NULL){
          ESP_LOGI("","No more RAM for reverb allPassBuf0!");
        } else {
          ESP_LOGI("","REVERB: allPassBuf0 : ");
          ESP_LOGI("","%d Bytes RAM allocated for reverb buffer, &=%#010x\r\n", sizeof(float) * ALLPASS_BUF_LEN_0 , allPassBuf0);
          memset(allPassBuf0, 0, sizeof(float) * ALLPASS_BUF_LEN_0);
        }
        allPassBuf1 = (float*)heap_caps_aligned_alloc( BYTE_ALIGN sizeof(float) * ALLPASS_BUF_LEN_1 , MALLOC_CAP );
        if( allPassBuf1 == NULL){
          ESP_LOGI("","No more RAM for reverb allPassBuf1!");
        } else {
          ESP_LOGI("","REVERB: allPassBuf1 : ");
          ESP_LOGI("","%d Bytes RAM allocated for reverb buffer, &=%#010x\r\n", sizeof(float) * ALLPASS_BUF_LEN_1, allPassBuf1);
          memset(allPassBuf1, 0, sizeof(float) * ALLPASS_BUF_LEN_1);
        }
        allPassBuf2 = (float*)heap_caps_aligned_alloc( BYTE_ALIGN sizeof(float) * ALLPASS_BUF_LEN_2 , MALLOC_CAP );
        if( allPassBuf2 == NULL){
          ESP_LOGI("","No more RAM for reverb allPassBuf2!");
        } else {
          ESP_LOGI("","REVERB: allPassBuf2 : ");
          ESP_LOGI("","%d Bytes RAM allocated for reverb buffer, &=%#010x\r\n", sizeof(float) * ALLPASS_BUF_LEN_2, allPassBuf2);
          memset(allPassBuf2, 0, sizeof(float) * ALLPASS_BUF_LEN_2);
        }
        
  		SetLevel( 1.0f );
  		SetTime( 0.5f );
  	}
		
    inline void SetTime( float value ){
      rev_time = 0.92f * value + 0.02f ;
      cf0_lim = (int)(rev_time * (float)(COMB_BUF_LEN_0));
      cf1_lim = (int)(rev_time * (float)(COMB_BUF_LEN_1));
      cf2_lim = (int)(rev_time * (float)(COMB_BUF_LEN_2));
      cf3_lim = (int)(rev_time * (float)(COMB_BUF_LEN_3));
      ap0_lim = (int)(rev_time * (float)(ALLPASS_BUF_LEN_0));
      ap1_lim = (int)(rev_time * (float)(ALLPASS_BUF_LEN_1));
      ap2_lim = (int)(rev_time * (float)(ALLPASS_BUF_LEN_2));
#ifdef DEBUG_FX
      ESP_LOGI("","reverb time: %0.3f\n", value);
#endif
    }
    
    inline void SetLevel( float value ){
      rev_level = value;
#ifdef DEBUG_FX
      ESP_LOGI("","reverb level: %0.3f\n", value);
#endif
    }
		
	private:
		float rev_time = 0.5f;
		float rev_level = 0.5f;
 
    float* combBuf0     = nullptr;
    float* combBuf1     = nullptr;
    float* combBuf2     = nullptr;
    float* combBuf3     = nullptr;
    float* allPassBuf0  = nullptr;
    float* allPassBuf1  = nullptr;
    float* allPassBuf2  = nullptr;    
 	
		//define pointer limits = delay time
		int cf0_lim, cf1_lim, cf2_lim, cf3_lim, ap0_lim, ap1_lim, ap2_lim;

    
    inline float Do_Comb0( float inSample ){
      int cf0_p = 0;
      float cf0_g = 0.805f;

      float readback = combBuf0[cf0_p];
      float newV = (float)readback * (float)cf0_g + (float)inSample;
      combBuf0[cf0_p] = newV;
      cf0_p++;
      if( cf0_p >= cf0_lim ){
        cf0_p = 0;
      }
      return readback;
    }

    inline float Do_Comb1( float inSample ){
      
      int cf1_p = 0;
      float cf1_g = 0.827f;

      float readback = combBuf1[cf1_p];
      float newV = (float)readback * (float)cf1_g + (float)inSample;
      combBuf1[cf1_p] = newV;
      cf1_p++;
      if( cf1_p >= cf1_lim ){
        cf1_p = 0;
      }
      return readback;
    }

    inline float Do_Comb2( float inSample ){
      int cf2_p = 0;
      float cf2_g = 0.783f;

      float readback = combBuf2[cf2_p];
      float newV = (float)readback * (float)cf2_g + (float)inSample;
      combBuf2[cf2_p] = newV;
      cf2_p++;
      if( cf2_p >= cf2_lim ){
        cf2_p = 0;
      }
      return readback;
    }

    inline float Do_Comb3( float inSample ){
      int cf3_p = 0;
      float cf3_g = 0.764f;

      float readback = combBuf3[cf3_p];
      float newV = (float)readback * (float)cf3_g + (float)inSample;
      combBuf3[cf3_p] = newV;
      cf3_p++;
      if( cf3_p >= cf3_lim ){
        cf3_p = 0;
      }
      return readback;
    }


    inline float Do_Allpass0( float inSample ){
      int ap0_p = 0;
      float ap0_g = 0.7f;

      float readback = allPassBuf0[ap0_p];
      readback += (float)(-ap0_g) * (float)inSample;
      float newV = (float)readback * (float)ap0_g + (float)inSample;
      allPassBuf0[ap0_p] = newV;
      ap0_p++;
      if( ap0_p >= ap0_lim ){
        ap0_p = 0;
      }
      return readback;
    }

    inline float Do_Allpass1( float inSample ){
      int ap1_p = 0;
      float ap1_g = 0.7f;

      float readback = allPassBuf1[ap1_p];
      readback += (float)(-ap1_g) * (float)inSample;
      float newV = (float)readback * (float)ap1_g + (float)inSample;
      allPassBuf1[ap1_p] = newV;
      ap1_p++;
      if( ap1_p >= ap1_lim ){
        ap1_p = 0;
      }
      return readback;
    }

    inline float Do_Allpass2( float inSample ){
      int ap2_p = 0;
      float ap2_g = 0.7f;

      float readback = allPassBuf2[ap2_p];
      readback += (float)(-ap2_g) * (float)inSample;
      float newV = (float)readback * (float)ap2_g + (float)inSample;
      allPassBuf2[ap2_p] = newV;
      ap2_p++;
      if( ap2_p >= ap2_lim ){
        ap2_p = 0;
      }
      return readback;
    }


};
