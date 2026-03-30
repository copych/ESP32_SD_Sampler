!Attention! ESP Arduino cores v.3.1.2 and 3.1.3 have some bug that won't allow i2s driver to install when PSRAM is enabled, please, avoid using these versions!

# ESP32 SD Sampler
ESP32 SD Sampler is a polyphonic music synthesizer, which can play PCM WAV samples directly from an SD (microSD) card connected to an ESP32-S3 or ESP32-P4.
Simple: one directory = one sample set. Plain text "sampler.ini" manages how samples to be spread over the keyboard.
The main difference, comparing to the projects available on the net, is that this sampler WON'T try to preload all the stuff into the RAM/PSRAM to play it on demand. So it's not limited in this way by the size of the memory chip and can take really huge (per-note true sampled multi-velocity several gigabytes) sample sets. It only requires that the card is freshly formatted FAT32 and has no or very few bad blocks (actually it requires that the WAV files are written with little or no fragmentation at all). On start it analyzes existing file allocation table (FAT) and forms it's own sample lookup table to be able to access data immediately, using SDMMC with a 4-bit wide bus.

# Features
* Easy to build and to customize Arduino code for ESP32S3/ESP32P4
* Hardware would cost you about $15, including a microSD card
* Audio output is 44100Hz 16bit stereo
* Direct read-only access to the sample sets on an SD/microSD card: it's based on custom sdmmc routines and it's fast
* Size of a sample set is only limited by the card size
* Polyphony of 15-20 stereo voices depending on your MCU and card's specs
* 16/24 bit WAV files supported
* Melodic and percussive sample sets supported
* ADSR envelope, per-note configurable
* Built-in Reverb FX
* MIDI control currently supports the following messages:
    * Note On
    * Note Off
    * Sustain (CC64)
    * Pitchbend
    * Attack time (CC73)
    * Release time (CC72)
    * Decay time (CC75)
    * Sustain level (CC76)
    * Reverb time (CC87)
    * Reverb level (CC88)
    * Reverb send (CC91)
* Human readable SAMPLER.INI controls initial parameters of a sample set globally, per-range and per-note
  
# YouTube Video

[![Video](https://img.youtube.com/vi/6Oe6QPwk1ak/maxresdefault.jpg)](https://youtu.be/6Oe6QPwk1ak?feature=shared)

# How To Build the Thing
* You need ```Arduino IDE``` installed, preferrably, version 1.8.x. (Version 2.x.x will probably also work, but you may face some unexpected issues)
* Next is ```ESP32 Arduino core```, version 2.0.17 seems to be the best choice (as for June 2024)
* Libraries required are:    
    * ```Fixed string library``` https://github.com/fatlab101/FixedString
    * ```Arduino MIDI library``` https://github.com/FortySevenEffects/arduino_midi_library
    * [optionally] If you want to use RGB LEDs, then also ```FastLED library``` is needed https://github.com/FastLED/FastLED
* Download the [zipped code](https://github.com/copych/ESP32_SD_Sampler/archive/refs/heads/main.zip) or use git commands to get the project files
* Unpack and place ```ESP32_SD_Sampler``` folder to your Arduino projects directory
* Open ```ESP32_SD_Sampler.ino``` file with Arduino IDE
* Connect your board via USB to your computer, select the corresponding ```port``` in the ```Tools``` menu and press the ```Upload``` button in the Arduino IDE

# Is it possible to run on ESP32 not S3?
For the time being the sampler only supports S3 variant. 
It's possible to rearrange classes in order to allocate members dynamically, and run it on an ESP32, but ESP32 has a strong limitation: memory segmentation (SRAM0, SRAM1, SRAM2 and segments within these parts like BSS, IRAM, DRAM etc) is hardware defined and has different performance. So it's quite a challenge to fit all the objects and buffers in appropriate memory regions. I have tried and managed to compile, but the performance was much worse so I rolled back. If someone would like to, please fork the repository and try.


# Polyphony
The maximum number of simultaneously sounding voices mainly depends on the following four factors:
* SD card tech specs
* The size of the allocated per-voice buffers (fast internal RAM required, which is also used by main program and audio effects)
* Required data rate (it depends on sample rates, number of channels and bit depths of both output bus and the sample files)
* CPU and memory performance (in fact, the low-level memory caching routines seem to be the bottleneck for now)
Each of the mentioned parameters has it's own limitations and even more, they are partly compete for the same resources. So the choosen configuration is always a compromise.

With the microSD cards that I have, my current setting is 17 stereo voices. I now set 7 sectors per read, which gives approx. 5 MB/s reading speed. Combined limitation is per-voice buffer size (i.e. how many sectors we read from the SD per request). The more the size, the more the speed. But the more the size, the more memory we need. In theory, 5 MB/s at 44100 Hz 16 bit stereo should give 29 voices polyphony, so there is probably a room to improve to get more simultaneous voices. But the limitation can also be caused by the computing power and by the internal cache performance.

PS. Of what I have tested, faster cards won't give you dramatical improvement in the matter of polyphony. I have tried a newer microSD which reads 8 sectors random blocks at apx. 7 MB/s, but only 20 voices I have managed to run at MAX.

# Velocity layers
There are currently 16 velocity layers (i.e. dynamic variants of each sampled note) which corresponds to the maximum count that I have found (https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html).


# SAMPLER.INI and examples
One should put the sampler.ini file to the same folder where the corresponding WAV files are stored. 

Ini syntax is described here: https://github.com/copych/ESP32_S3_Sampler/blob/main/sampler_ini_syntax.md
the doc is under development and will be improved.

Here is an example of a sampler.ini file for the Salamander Grand Piano WAV-set:
```
[sampleset]
title = Salamander Grand Piano
; type=percussive/melodic
type=melodic
; Are all the samples of equal loudness? If true then we apply amplification according to the midi note velocity.
normalized=true
; Overall Amplification
amplify = 1.2

; ADSR times in seconds
attackTime = 0.0
decayTime = 12.0
releaseTime = 0.1
; ADSR sustain level 0.0 - 1.0
sustainLevel = 0.0

# !!!!!!!!!!!!!!! "enveloped=bool" is no longer supported, as the ADSR is always applyed

# !!!!!!!!!!!!!!! [envelope] section is no longer supported, [sampleset] now holds global settings

[filename]
# Filename elements recognized:
; <NAME> - note name in sharp (#) or flat(b) notation, i.e. both Eb and D# are valid
; <OCTAVE> - octave number
; <MIDINOTE> - sometimes sample file name contains midi note number
; <NUMBER> - parsed, but not used for now: i.e. some numbers initially used for naming, sorting or whatever
; <VELO> velocity layer
; <INSTR> instruments names (mostly percussion) used in filenames, initially they are collected from this ini file
; elements without brackets will be treated as some constant string delimeters

; these elements are case insensitive, heading and trailing spaces are trimmed.
template=<NAME><OCTAVE>v<VELO>

; we must provide these variants along with the template. The order is important: from the most quiet
; to the most loud, comma separated
veloVariants = 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16

; in addition we can provide optional info on how velocity layers shall be spread over
; the whole range of 0-127, giving the upper limits of each range
veloLimits = 26,34,36,43,46,50,56,64,72,80,88,96,104,112,121,127

[range]
# notes without dampers
first = F#6
last = G8
noteoff = false

# [note] sections 
; instr=instrument_name(as in filename)
; noteoff=0/1 (0=ignore, 1=end note)
; speed=float_number(1.0 means unchanged (default), 1.2 means 20% faster which is higher pitch, 0.9 is 10% lower)
```

another sampler.ini example is for a drumkit MuldjordKit-SFZ-20201018, all the samples moved to a single folder
```
[sampleset]
title = Generic Drum Kit

; type=percussive/melodic
; different technics used while spreading samples over the clavier
; percussive type allows per-note fx-send level
type=percussive

; Are all the samples of equal loudness? If true then we apply amplification according to the midi note velocity.
normalized=true

; ADSR times in seconds
attackTime = 0.0
decayTime = 0.05
releaseTime = 0.2
; ADSR sustain level 0.0 - 1.0
sustainLevel = 1.0

# !!!!!!!!!!!!!!! "enveloped=bool" is no longer supported, as the ADSR is always applyed

# !!!!!!!!!!!!!!! [envelope] section is no longer supported, [sampleset] now holds global settings

[filename]
# Filename elements recognized:
; <NAME> - note name=name in sharp (#) or flat(b) notation, i.e. both Eb and D# are valid
; <OCTAVE> - octave number
; <MIDINOTE> - sometimes sample file name contains midi note number
; <NUMBER> - parsed, but not used for now: i.e. some numbers initially used for naming, sorting or whatever
; <VELO> velocity layer
; <INSTR> instruments names (mostly percussion) used in filenames, initially they are collected from this ini file
; only instrument names mentioned in this ini (in [note] or [range] sections) will be recognized 
; elements without brackets will be treated as some constant string delimeters
; these elements are case insensitive, heading and trailing spaces are trimmed.
; template should be specified first in this section
template=<VELO>-<INSTR>

; we must provide these variants. The order is important: from the most quiet to the most loud, comma separated
veloVariants = 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16


[group]
notes = F#1,A#1,G#1

notes = G#2,A2

notes = C#2,D2


# [note] sections
; name = notename in sharp (#) or flat(b) notation, i.e. both Eb and D# are valid
; instr = instrument_name(as in filename)
; noteoff = 0/1 (0 is 'ignore', 1 is 'end note'), also yes/no, true/false are recognized
; speed = float_number (1.0 means unchanged (default), 1.2 means 20% faster which is higher pitch, 0.9 is 10% lower)
; only instrument names mentioned in this ini (in [note] or [range] sections) will be recognized 

[note]
name=b0
instr=kdrumL
noteoff=false

[note]
name=c1
instr=kdrumR
noteoff=no

[note]
name=C#1
instr=SnareRest
noteoff=0

[note]
name=D1
instr=Snare
noteoff=yes

[note]
name=D#1
; assuming we have "#-SnareRest (2).WAV" files in the drumkit folder after copying all the files into a single dir.
instr=SnareRest (2)
noteoff=true

[note]
name=E1
; assuming we have "#-Snare (2).WAV" files in the drumkit folder after copying all the files into a single dir.
instr=snare (2)
noteoff=0

[note]
name=F1
instr=Tom1
noteoff=0

[note]
name=G1
instr=Tom2
noteoff=0

[note]
name=A1
instr=Tom3
noteoff=0

[note]
name=B1
instr=Tom4
noteoff=0

[note]
name=C2
instr=Tom4
noteoff=0
speed=1.15

[note]
name=F#1
instr=HiHatClosed
noteoff=0

[note]
name=A#1
instr=HiHatOpen
noteoff=1

[note]
name=C#2
instr=CrashL
noteoff=0

[note]
name=D2
instr=CrashL
noteoff=true

[note]
name=E2
instr=China
noteoff=0

[note]
name=G#2
instr=CrashR
noteoff=0

[note]
name=A2
instr=CrashR
noteoff=yes

[note]
name=D#2
instr=RideL
noteoff=0

[note]
name=B2
instr=RideR
noteoff=0

[note]
name=F2
instr=RideLBell
noteoff=0

[note]
name=A#2
instr=RideRBell
noteoff=0
```
