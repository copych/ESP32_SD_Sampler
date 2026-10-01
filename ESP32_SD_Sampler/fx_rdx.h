#pragma once

// Two-slot effects host.  The algorithms and parameter mappings are ported
// from RDX-Reface-DX-emu (Copych, MIT), with the long delay lines kept in
// PSRAM: the sampler's internal RAM must remain available for SD streaming.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cmath>
#include <atomic>

enum RdxFxId : uint8_t {
  RDX_FX_THRU = 0, RDX_FX_DISTORTION, RDX_FX_TOUCHWAH, RDX_FX_CHORUS,
  RDX_FX_FLANGER, RDX_FX_PHASER, RDX_FX_DELAY, RDX_FX_REVERB, RDX_FX_COUNT
};

class RdxFxHost {
 public:
  static constexpr uint8_t kSlots = 2;
  // One second of stereo delay per slot. Reverb, chorus and flanger reuse it.
  static constexpr uint32_t kArenaFloats = SAMPLE_RATE * 2;

  bool init() {
    for (uint8_t s = 0; s < kSlots; ++s) {
      _arena[s] = static_cast<float*>(heap_caps_calloc(
          kArenaFloats, sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!_arena[s]) {
        ESP_LOGE("FX", "slot %u PSRAM allocation failed (%u KiB)", s,
                 unsigned(kArenaFloats * sizeof(float) / 1024));
        return false;
      }
      _type[s] = RDX_FX_THRU;
      _param[s][0] = 64; _param[s][1] = 64;
      reset(s);
    }
    // Preserve the sampler's former default: a moderate reverb in slot 1.
    setSlot(0, RDX_FX_REVERB, 64, 89);
    ESP_LOGI("FX", "RDX effects ready: 2 x %u KiB PSRAM", unsigned(kArenaFloats * sizeof(float) / 1024));
    return true;
  }

  void setSlot(uint8_t slot, RdxFxId type, uint8_t p1 = 64, uint8_t p2 = 64) {
    if (slot >= kSlots || type >= RDX_FX_COUNT || !_arena[slot]) return;
    // A slot change clears a PSRAM arena. Core0 bypasses effects for those
    // blocks, so it never observes a half-reset delay line.
    while (_edit.test_and_set(std::memory_order_acquire)) taskYIELD();
    _type[slot] = type; _param[slot][0] = p1; _param[slot][1] = p2; reset(slot);
    _edit.clear(std::memory_order_release);
    ESP_LOGI("FX", "slot %u = %u, %u, %u", slot, unsigned(type), p1, p2);
  }
  void setParam(uint8_t slot, uint8_t parameter, uint8_t value) {
    if (slot < kSlots && parameter < 2) _param[slot][parameter] = value;
  }
  RdxFxId type(uint8_t slot) const { return slot < kSlots ? _type[slot] : RDX_FX_THRU; }
  uint8_t param(uint8_t slot, uint8_t p) const { return (slot < kSlots && p < 2) ? _param[slot][p] : 0; }

  // Keep the substantial DSP dispatch in flash.  Marking it IRAM forced every
  // inlined effect body into one Xtensa literal pool and exceeded its range.
  void __attribute__((noinline)) process(float* l, float* r, uint32_t n) {
    if (_edit.test_and_set(std::memory_order_acquire)) return;
    for (uint8_t s = 0; s < kSlots; ++s) switch (_type[s]) {
      case RDX_FX_DISTORTION: distortion(s, l, r, n); break;
      case RDX_FX_TOUCHWAH:   touchWah(s, l, r, n); break;
      case RDX_FX_CHORUS:     chorus(s, l, r, n); break;
      case RDX_FX_FLANGER:    flanger(s, l, r, n); break;
      case RDX_FX_PHASER:     phaser(s, l, r, n); break;
      case RDX_FX_DELAY:      delay(s, l, r, n); break;
      case RDX_FX_REVERB:     reverb(s, l, r, n); break;
      default: break;
    }
    _edit.clear(std::memory_order_release);
  }

 private:
  struct State {
    uint32_t write = 0; float phase = 0, env = 0, feedbackL = 0, feedbackR = 0;
    float lpL = 0, lpR = 0, dcIn = 0, dcOut = 0; float zL[6] = {}, zR[6] = {};
    uint32_t combIndex[2][4] = {}, allIndex[2][2] = {};
    uint16_t combLen[2][4] = {}, allLen[2][2] = {}; float combLp[2][4] = {};
    uint8_t reverbCombs = 4, reverbAllpasses = 2;
  } _state[kSlots];
  float* _arena[kSlots] = {};
  RdxFxId _type[kSlots] = {};
  volatile uint8_t _param[kSlots][2] = {};
  std::atomic_flag _edit = ATOMIC_FLAG_INIT;

  static inline float clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
  static inline float sat(float x) { x = clamp(x, -1.f, 1.f); return 1.5f * (x - x*x*x*.3334f); }
  static inline float wrap(float x) { return x - floorf(x); }
  static inline float tri(float x) { x = wrap(x); return 1.f - fabsf(2.f*x - 1.f); }
  static inline float lfoRate(uint8_t p) { return .03f + 40.f * (float(p) / 127.f) * (float(p) / 127.f); }
  static inline float read(float* b, uint32_t size, float pos) {
    while (pos < 0) pos += size;
    while (pos >= size) pos -= size;
    uint32_t i = uint32_t(pos), j = i + 1; if (j == size) j = 0;
    return b[i] + (b[j] - b[i]) * (pos - i);
  }

  void reset(uint8_t s) {
    if (!_arena[s]) return;
    memset(_arena[s], 0, kArenaFloats * sizeof(float));
    _state[s] = {};
    // RDX reverb topology, with combs and allpasses laid out in the PSRAM arena.
    // S3 PSRAM is too slow for the full topology at the sampler block rate;
    // use a compact room there while P4 keeps all RDX delay lines.
    if (_type[s] == RDX_FX_REVERB) {
      #if defined(CONFIG_IDF_TARGET_ESP32S3)
      const float combMs[4] = {17.3f, 23.7f, 0.f, 0.f};
      const float allMs[2] = {3.2f, 0.f};
      _state[s].reverbCombs = 2; _state[s].reverbAllpasses = 1;
      #else
      const float combMs[4] = {47.3f, 53.7f, 71.1f, 89.5f};
      const float allMs[2] = {9.7f, 3.2f};
      #endif
      uint32_t used = 0;
      for (int ch=0; ch<2; ++ch) for (int i=0; i<_state[s].reverbCombs; ++i) {
        _state[s].combLen[ch][i] = uint16_t(combMs[i] * SAMPLE_RATE / 1000.f) + ch * 17; used += _state[s].combLen[ch][i];
      }
      for (int ch=0; ch<2; ++ch) for (int i=0; i<_state[s].reverbAllpasses; ++i) {
        _state[s].allLen[ch][i] = uint16_t(allMs[i] * SAMPLE_RATE / 1000.f) + i + ch; used += _state[s].allLen[ch][i];
      }
      if (used > kArenaFloats) _type[s] = RDX_FX_THRU;
    }
  }

  void distortion(uint8_t s, float* l, float* r, uint32_t n) {
    const float drive = _param[s][0] / 127.f, tone = _param[s][1] / 127.f;
    const float gain = 1.f + 24.f*drive*drive, makeup = .4f/(1.f+.25f*drive*drive);
    const float a = 1.f-expf(-6.2831853f*(300.f+7700.f*tone)/SAMPLE_RATE);
    for (uint32_t i=0;i<n;++i) { float xL=.5f*sat(l[i]*gain)+.5f*l[i], xR=.5f*sat(r[i]*gain)+.5f*r[i];
      _state[s].lpL += a*(xL-_state[s].lpL); _state[s].lpR += a*(xR-_state[s].lpR); l[i]=_state[s].lpL*makeup; r[i]=_state[s].lpR*makeup; }
  }
  void touchWah(uint8_t s, float* l, float* r, uint32_t n) {
    const float sens=_param[s][0]/127.f, res=.4f+.7f*_param[s][1]/127.f;
    for(uint32_t i=0;i<n;++i) { float inL=l[i],inR=r[i], level=.5f*(fabsf(inL)+fabsf(inR));
      _state[s].env += (level-_state[s].env)*(level>_state[s].env ? .02f+.08f*sens : .003f);
      const float f=clamp(.02f+.38f*_state[s].env*sens,.02f,.4f), a=(1-f)/(1+f);
      float xL=inL+_state[s].feedbackL*(.6f+.2f*res),xR=inR+_state[s].feedbackR*(.6f+.2f*res);
      for(int q=0;q<6;++q){float y=-a*xL+_state[s].zL[q];_state[s].zL[q]=xL+a*y;xL=y;y=-a*xR+_state[s].zR[q];_state[s].zR[q]=xR+a*y;xR=y;}
      _state[s].feedbackL=xL*res;_state[s].feedbackR=xR*res;l[i]=.3f*inL+.7f*xL;r[i]=.3f*inR+.7f*xR; }
  }
  void chorus(uint8_t s, float* l, float* r, uint32_t n) {
    constexpr uint32_t N=4096; float* bl=_arena[s],*br=bl+N; const float dep=(.0005f+.0045f*_param[s][0]/127.f)*SAMPLE_RATE, rate=lfoRate(_param[s][1])*.2436f;
    for(uint32_t i=0;i<n;++i){float off=sinf(_state[s].phase*6.2831853f)*dep;float dl=read(bl,N,float(_state[s].write)-1323.f-off),dr=read(br,N,float(_state[s].write)-1323.f+off);bl[_state[s].write]=l[i];br[_state[s].write]=r[i];l[i]+=.25f*dl;r[i]+=.25f*dr;_state[s].write=(_state[s].write+1)&(N-1);_state[s].phase=wrap(_state[s].phase+rate/SAMPLE_RATE);}
  }
  void flanger(uint8_t s, float* l, float* r, uint32_t n) {
    constexpr uint32_t N=2048; float* bl=_arena[s],*br=bl+N; const float dep=(.0003f+.008f*_param[s][0]/127.f)*SAMPLE_RATE,rate=lfoRate(_param[s][1])*.5f;
    for(uint32_t i=0;i<n;++i){float o=tri(_state[s].phase)*dep+48.f;float dl=read(bl,N,float(_state[s].write)-o),dr=read(br,N,float(_state[s].write)-(dep+96.f-o));bl[_state[s].write]=l[i];br[_state[s].write]=r[i];l[i]=.55f*l[i]+.45f*dl;r[i]=.55f*r[i]+.45f*dr;_state[s].write=(_state[s].write+1)&(N-1);_state[s].phase=wrap(_state[s].phase+rate/SAMPLE_RATE);}
  }
  void phaser(uint8_t s, float* l, float* r, uint32_t n) {
    const float depth=.061f+.0498f*_param[s][0],rate=.0936f+lfoRate(_param[s][1])*.188f;
    _state[s].phase=wrap(_state[s].phase+rate*n/SAMPLE_RATE); float ratio=powf(2.f,(depth*tri(_state[s].phase))/12.f); float freq[2]={90.f*ratio,500.f*ratio}; float a[2];for(int q=0;q<2;++q){float w=6.2831853f*freq[q]/SAMPLE_RATE;a[q]=(1-w)/(1+w);}
    for(uint32_t i=0;i<n;++i){float inL=l[i],inR=r[i],xL=inL+_state[s].feedbackL*.1f,xR=inR+_state[s].feedbackR*.1f;for(int q=0;q<2;++q){float y=-a[q]*xL+_state[s].zL[q];_state[s].zL[q]=xL+a[q]*y;xL=y;y=-a[q]*xR+_state[s].zR[q];_state[s].zR[q]=xR+a[q]*y;xR=y;}_state[s].feedbackL=xL;_state[s].feedbackR=xR;l[i]=.47f*inL+.53f*xL;r[i]=.47f*inR+.53f*xR;}
  }
  void delay(uint8_t s, float* l, float* r, uint32_t n) {
    const uint32_t N=SAMPLE_RATE; float* bl=_arena[s],*br=bl+N; const float fb=.5f*_param[s][0]/127.f,mix=.1f+.15f*_param[s][0]/127.f;const uint32_t len=uint32_t((.0116f+1.1894f*_param[s][1]/127.f)*SAMPLE_RATE);
    for(uint32_t i=0;i<n;++i){uint32_t out=(_state[s].write+N-len)%N;float dl=bl[out],dr=br[out];bl[_state[s].write]=l[i]+dl*fb;br[_state[s].write]=r[i]+dr*fb;l[i]=(1-mix)*l[i]+mix*dl;r[i]=(1-mix)*r[i]+mix*dr;if(++_state[s].write==N)_state[s].write=0;}
  }
  void reverb(uint8_t s, float* l, float* r, uint32_t n) {
    State& st=_state[s]; const float depth=.2f*_param[s][0]/127.f,time=_param[s][1]/127.f,rt60=.25f*powf(24.f,time); uint32_t offsets[2][4],allOff[2][2],at=0;
    for(int ch=0;ch<2;++ch)for(int q=0;q<st.reverbCombs;++q){offsets[ch][q]=at;at+=st.combLen[ch][q];}for(int ch=0;ch<2;++ch)for(int q=0;q<st.reverbAllpasses;++q){allOff[ch][q]=at;at+=st.allLen[ch][q];}
    // RDX updates these gains when the time parameter changes, never in the
    // inner sample loop. Doing eight powf() calls per frame starves IDLE0.
    float combGain[4];
    for (int q=0; q<st.reverbCombs; ++q)
      combGain[q] = fminf(powf(10.f, -3.f * (st.combLen[0][q] / float(SAMPLE_RATE)) / rt60), .95f);
    const float combMix = 1.f / st.reverbCombs;
    for(uint32_t k=0;k<n;++k){float in=.5f*(l[k]+r[k]);st.dcOut=in-st.dcIn+.996f*st.dcOut;st.dcIn=in;float wet[2]={};for(int ch=0;ch<2;++ch){for(int q=0;q<st.reverbCombs;++q){float* b=_arena[s]+offsets[ch][q];uint32_t& ix=st.combIndex[ch][q];float y=b[ix];st.combLp[ch][q]=y+.3f*(st.combLp[ch][q]-y);b[ix]=st.dcOut+st.combLp[ch][q]*combGain[q];if(++ix==st.combLen[ch][q])ix=0;wet[ch]+=y;}wet[ch]*=combMix;for(int q=0;q<st.reverbAllpasses;++q){float* b=_arena[s]+allOff[ch][q];uint32_t& ix=st.allIndex[ch][q];float y=b[ix];b[ix]=wet[ch]+.7f*y;wet[ch]=y-.7f*wet[ch];if(++ix==st.allLen[ch][q])ix=0;}}l[k]=(1-depth)*l[k]+depth*wet[0];r[k]=(1-depth)*r[k]+depth*wet[1];}
  }
};
