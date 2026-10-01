#pragma once

// Global 


/*  reminder
RPNs
0x0000 – Pitch bend range
0x0001 – Fine tuning
0x0002 – Coarse tuning
0x0003 – Tuning program change
0x0004 – Tuning bank select
0x0005 – Modulation depth range

undefined MIDI CCs:
CC 3, 9, 14, 15
CC 20 - 31
CC 85 - 87
CC 89, 90
CC 102 - 119

*/

#define CC_PORTATIME    5
#define CC_VOLUME       7
#define CC_PAN          10
#define CC_SUSTAIN      64 // +
#define CC_PORTAMENTO   65

// RDX two-slot chain: value 0..7 selects THRU, DISTORTION, TOUCHWAH,
// CHORUS, FLANGER, PHASER, DELAY or REVERB.  Parameters are effect-specific.
#define CC_FX1_TYPE     20
#define CC_FX2_TYPE     21
#define CC_FX1_PARAM1   22
#define CC_FX1_PARAM2   23
#define CC_FX2_PARAM1   24
#define CC_FX2_PARAM2   25

#define CC_WAVEFORM     70
#define CC_RESO         71 
#define CC_CUTOFF       74 

#define CC_ENV_ATTACK   73 // +
#define CC_ENV_RELEASE  72 // +
#define CC_ENV_DECAY    75 // +
#define CC_ENV_SUSTAIN  76 // +

#define CC_DELAY_TIME   84 
#define CC_DELAY_FB     85 
#define CC_DELAY_LVL    86 
#define CC_REVERB_TIME  87 // +
#define CC_REVERB_LVL   88 // +

#define CC_REVERB_SEND  91 // +
#define CC_DELAY_SEND   92
#define CC_OVERDRIVE    93
#define CC_DISTORTION   94
#define CC_COMPRESSOR   95 

#define CC_RESET_CCS    121
#define CC_NOTES_OFF    123
#define CC_SOUND_OFF    120

#define NRPN_LSB_NUM    98  
#define NRPN_MSB_NUM    99
#define RPN_LSB_NUM     100
#define RPN_MSB_NUB     101
#define DATA_ENTRY_MSB  6
#define DATA_ENTRY_LSB  38
 
