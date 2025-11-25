

//#define DEBUG_MASTER_OUT


#if ESP_ARDUINO_VERSION_MAJOR < 3

#include "driver/i2s.h"
  // Arduino cores prior to 3.0.0

const i2s_port_t i2s_num = I2S_NUM_0; // i2s port number

void i2sInit() {
  pinMode(I2S_BCLK_PIN, OUTPUT);
  pinMode(I2S_DOUT_PIN, OUTPUT);
  pinMode(I2S_WCLK_PIN, OUTPUT);
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX ),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S ),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2,
    .dma_buf_count = DMA_NUM_BUF,
    .dma_buf_len = DMA_BUF_LEN,
    .use_apll = true,
  };

  i2s_pin_config_t i2s_pin_config = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num =  I2S_WCLK_PIN,
    .data_out_num = I2S_DOUT_PIN
  };

  i2s_driver_install(i2s_num, &i2s_config, 0, NULL);

  i2s_set_pin(i2s_num, &i2s_pin_config);
  i2s_zero_dma_buffer(i2s_num);

  ESP_LOGI("","I2S is started: BCK %d, WCK %d, DAT %d\r\n", I2S_BCLK_PIN, I2S_WCLK_PIN, I2S_DOUT_PIN);
}


void i2sDeinit() {
  i2s_zero_dma_buffer(i2s_num);
  i2s_driver_uninstall(i2s_num);
}


static void i2s_output () {
  // now out_buf is ready, output
  size_t bytes_written;


  for (int i=0; i < DMA_BUF_LEN; i++) {
    out_buf[i*2] = (float)0x7fff * mix_buf_l[i]; 
    out_buf[i*2+1] = (float)0x7fff * mix_buf_r[i];
   // if (i%4==0) ESP_LOGI("",out_buf[i*2]);
   // if (out_buf[i*2]) ESP_LOGI(""," %d\r\n ", out_buf[i*2]);
  }
  i2s_write(i2s_num, out_buf, sizeof(out_buf), &bytes_written, portMAX_DELAY);

}

#else
  // Arduino core 3.0.0 and up


#include "driver/i2s_std.h"
i2s_chan_handle_t tx_handle;
i2s_chan_handle_t rx_handle;

const i2s_port_t i2s_num = I2S_NUM_0; // i2s port number

void i2sInit() {
  pinMode(I2S_BCLK_PIN, OUTPUT);
  pinMode(I2S_DOUT_PIN, OUTPUT);
  pinMode(I2S_WCLK_PIN, OUTPUT);

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(i2s_num, I2S_ROLE_MASTER);
    chan_cfg.dma_frame_num = DMA_BUF_LEN;
    chan_cfg.dma_desc_num = DMA_NUM_BUF;
  i2s_new_channel(&chan_cfg, &tx_handle, NULL);
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      //.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = (gpio_num_t)I2S_BCLK_PIN,
          .ws = (gpio_num_t)I2S_WCLK_PIN,
          .dout = (gpio_num_t)I2S_DOUT_PIN,
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv = false,
          },
      },
  };

  i2s_channel_init_std_mode(tx_handle, &std_cfg);
  i2s_channel_enable(tx_handle);
  
  ESP_LOGI("","I2S is started: BCK %d, WCK %d, DAT %d\r\n", I2S_BCLK_PIN, I2S_WCLK_PIN, I2S_DOUT_PIN);
}


static void i2s_output () {
  // now out_buf is ready, output
  size_t* bytes_written;
  for (int i=0; i < DMA_BUF_LEN; i++) {
    out_buf[i*2] = (float)0x7fff * mix_buf_l[i]; 
    out_buf[i*2+1] = (float)0x7fff * mix_buf_r[i];
  } 
  i2s_channel_write(tx_handle, out_buf, sizeof(out_buf), bytes_written, portMAX_DELAY);
}


void i2sDeinit() {
  /* Have to stop the channel before deleting it */
  i2s_channel_disable(tx_handle);
  /* If the handle is not needed any more, delete it to release the channel resources */
  i2s_del_channel(tx_handle);
}

#endif


static void mixer() { // sum buffers 
#ifdef DEBUG_MASTER_OUT
  float meter = 0.0f;
#endif
  const float attenuator = 0.1f;
  float sampler_out_l, sampler_out_r;
  float mono_mix;
  float dly_l, dly_r;
  float rvb_l, rvb_r;
  
    for (int i=0; i < DMA_BUF_LEN; i++) {
      
      sampler_out_l = (float)sampler_l[i] * (float)attenuator;
      sampler_out_r = (float)sampler_r[i] * (float)attenuator;

  //    DJFilter.Process(&sampler_out_l, &sampler_out_r);

  //    Drive.Process(&sampler_out_l, &sampler_out_r);               // overdrive // make it stereo firstly
  //    Distortion.Process(&sampler_out_l, &sampler_out_r);             // distortion // make it stereo firstly
/*
      dly_l = sampler_out_l * Sampler.getDelaySendLevel(); // delay bus
      dly_r = sampler_out_r * Sampler.getDelaySendLevel();
      Delay.Process( &dly_l, &dly_r );
 
      sampler_out_l += dly_l;
      sampler_out_r += dly_r;
*/


      rvb_l = (float)sampler_out_l * (float)Sampler.getReverbSendLevel(); // reverb bus
      rvb_r = (float)sampler_out_r * (float)Sampler.getReverbSendLevel();
      Reverb.Process( &rvb_l, &rvb_r );
      
      sampler_out_l += (float)rvb_l;
      sampler_out_r += (float)rvb_r;

      
      mono_mix = 0.5f * ((float)sampler_out_l + (float)sampler_out_r);
      
  //    Comp.Process( mono_mix * 0.25f);  // calc compressor gain, may be side-chain driven 
            
  //    mix_buf_l[i] = Comp.Apply(sampler_out_l);
  //    mix_buf_r[i] = Comp.Apply(sampler_out_r);
      mix_buf_l[i] = ((float)sampler_out_l);
      mix_buf_r[i] = ((float)sampler_out_r);

#ifdef DEBUG_MASTER_OUT
      if ( i % 16 == 0) meter = (float)meter * 0.95f + fabs( mono_mix); 
#endif

  // if none of the following limitters is engaged, digital clipping can occur

   //   mix_buf_l[i] = fclamp(mix_buf_l[i] , -1.0f, 1.0f); // clipper
   //   mix_buf_r[i] = fclamp(mix_buf_r[i] , -1.0f, 1.0f);

      mix_buf_l[i] = fast_shape( mix_buf_l[i]); // soft limitter/saturator
      mix_buf_r[i] = fast_shape( mix_buf_r[i]);
   }
   
#ifdef DEBUG_MASTER_OUT
  meter *= 0.95f;
  meter += fabs(mono_mix); 
  ESP_LOGI("","out= %0.5f\r\n", meter);
#endif
}

static void  sampler_generate_buf() {
  for (int i=0; i < DMA_BUF_LEN; i++){
    Sampler.getSample(sampler_l[i], sampler_r[i]) ;
  }
}
