// #define DEBUG_MASTER_OUT


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
  size_t bytes_written = 0;


  for (int i=0; i < DMA_BUF_LEN; i++) {
    out_buf[i*2] = (float)0x7fff * mix_buf_l[i]; 
    out_buf[i*2+1] = (float)0x7fff * mix_buf_r[i];
   // if (i%4==0) ESP_LOGI("",out_buf[i*2]);
   // if (out_buf[i*2]) ESP_LOGI(""," %d ", out_buf[i*2]);
  }
  const esp_err_t status = i2s_write(i2s_num, out_buf, sizeof(out_buf), &bytes_written, portMAX_DELAY);
#if DEBUG_AUDIO_DIAGNOSTICS
  if (status != ESP_OK || bytes_written != sizeof(out_buf)) {
    audio_debug::audio.mark(audio_debug::I2S_ERROR);
    audio_debug::audio.event(audio_debug::I2S_ERROR, micros() / 1000, bytes_written, uint32_t(status));
  }
#endif

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
  size_t bytes_written = 0;
  for (int i=0; i < DMA_BUF_LEN; i++) {
    out_buf[i*2] = (float)0x7ffe * mix_buf_l[i] + 1; 
    out_buf[i*2+1] = (float)0x7ffe * mix_buf_r[i] + 1;
  } 
  const esp_err_t status = i2s_channel_write(tx_handle, out_buf, sizeof(out_buf), &bytes_written, portMAX_DELAY);
#if DEBUG_AUDIO_DIAGNOSTICS
  if (status != ESP_OK || bytes_written != sizeof(out_buf)) {
    audio_debug::audio.mark(audio_debug::I2S_ERROR);
    audio_debug::audio.event(audio_debug::I2S_ERROR, micros() / 1000, bytes_written, uint32_t(status));
  }
#endif
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
  const float masterTarget = Sampler.getMasterVolume();
  static float masterGain = 1.0f;
#if DEBUG_AUDIO_DIAGNOSTICS
  uint32_t clipped = 0, nonfinite = 0;
  float peak = 0, jump = 0;
  static float previousL = 0, previousR = 0;
  static float previousDeltaL = 0, previousDeltaR = 0;
  static bool edgePrimed = false, wasClipping = false;
#endif
  float sampler_out_l, sampler_out_r;
  float mono_mix;
  
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


      mix_buf_l[i] = sampler_out_l;
      mix_buf_r[i] = sampler_out_r;
    }

  // RDX effects operate on a complete audio block. Their long state buffers
  // are in PSRAM, so this does not compete with the SD streaming buffers.
  Effects.process(mix_buf_l, mix_buf_r, DMA_BUF_LEN);

  for (int i=0; i < DMA_BUF_LEN; i++) {
      sampler_out_l = mix_buf_l[i];
      sampler_out_r = mix_buf_r[i];
      // Smooth GUI volume over a few milliseconds, including effect tails.
      masterGain += 0.005f * (masterTarget - masterGain);
      sampler_out_l *= masterGain;
      sampler_out_r *= masterGain;
      mono_mix = 0.5f * (sampler_out_l + sampler_out_r);
      
  //    Comp.Process( mono_mix * 0.25f);  // calc compressor gain, may be side-chain driven 
            
  //    mix_buf_l[i] = Comp.Apply(sampler_out_l);
  //    mix_buf_r[i] = Comp.Apply(sampler_out_r);
      mix_buf_l[i] = sampler_out_l;
      mix_buf_r[i] = sampler_out_r;
#if DEBUG_AUDIO_DIAGNOSTICS
      if (!isfinite(sampler_out_l) || !isfinite(sampler_out_r)) ++nonfinite;
      else {
        peak = fmaxf(peak, fmaxf(fabsf(sampler_out_l), fabsf(sampler_out_r)));
        if (fabsf(sampler_out_l) > 1 || fabsf(sampler_out_r) > 1) ++clipped;
        const float l = fclamp(sampler_out_l, -1.f, 1.f), r = fclamp(sampler_out_r, -1.f, 1.f);
        const float deltaL = l - previousL, deltaR = r - previousR;
        const float sampleJump = fmaxf(fabsf(deltaL), fabsf(deltaR));
        jump = fmaxf(jump, sampleJump);
        if (edgePrimed) {
          // Second difference: a sharp change of slope is a better click signature
          // than a large first difference, which is common in valid HF audio.
          const float edge = fmaxf(fabsf(deltaL - previousDeltaL),
                                   fabsf(deltaR - previousDeltaR));
          if (edge >= .015f) {
            const uint32_t edgePpm = audio_debug::level(edge, 0);
            audio_debug::audio.mark(audio_debug::EDGE_EVENT, edgePpm);
            // Keep the tiny event queue useful: only stronger edges get timestamps.
            if (edge >= .025f)
              audio_debug::audio.event(audio_debug::EDGE_EVENT, micros() / 1000, i, edgePpm);
          }
        } else edgePrimed = true;
        previousDeltaL = deltaL; previousDeltaR = deltaR;
        previousL = l; previousR = r;
      }
#endif

#ifdef DEBUG_MASTER_OUT
      if ( i % 16 == 0) meter = (float)meter * 0.95f + fabs( mono_mix); 
#endif

  // if none of the following limitters is engaged, digital clipping can occur

      mix_buf_l[i] = fclamp(mix_buf_l[i] , -1.0f, 1.0f); // clipper
      mix_buf_r[i] = fclamp(mix_buf_r[i] , -1.0f, 1.0f);

   //   mix_buf_l[i] = fast_shape( mix_buf_l[i]); // soft limitter/saturator
   //   mix_buf_r[i] = fast_shape( mix_buf_r[i]);
   }
   
#if DEBUG_AUDIO_DIAGNOSTICS
  audio_debug::audio.mark(audio_debug::MIX_CLIP, audio_debug::level(peak, 0), clipped);
  audio_debug::audio.mark(audio_debug::MIX_NONFINITE, 0, nonfinite);
  audio_debug::audio.mark(audio_debug::OUTPUT_JUMP, audio_debug::level(jump, 0), jump >= .05f ? 1 : 0);
  if (clipped && !wasClipping)
    audio_debug::audio.event(audio_debug::MIX_CLIP, micros() / 1000, 0, audio_debug::level(peak, 0));
  wasClipping = clipped != 0;
#endif
#ifdef DEBUG_MASTER_OUT
  meter *= 0.95f;
  meter += fabs(mono_mix); 
  ESP_LOGI("","out= %0.5f", meter);
#endif
}

static void  sampler_generate_buf() {
  for (int i=0; i < DMA_BUF_LEN; i++){
    Sampler.getSample(sampler_l[i], sampler_r[i]) ;
  }
}
