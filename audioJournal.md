# Android Audio: A Complete Deep-Dive Journal

> **Audience:** Senior Android app developer (5+ years) transitioning into AOSP internals.
> You know MediaPlayer, ExoPlayer, AudioManager, AudioRecord. This journal explains *what they actually do underneath*.

---

## Table of Contents

1. [The Big Picture — Android Audio Architecture](#1-the-big-picture--android-audio-architecture)
2. [Terminology Glossary](#2-terminology-glossary)
3. [Hardware Anatomy of an Audio Subsystem](#3-hardware-anatomy-of-an-audio-subsystem)
4. [Scenario 1 — Playing an MP3 Bundled Inside the APK](#4-scenario-1--playing-an-mp3-bundled-inside-the-apk)
5. [Scenario 2 — Playing an MP3 from SD Card / Internal Storage](#5-scenario-2--playing-an-mp3-from-sd-card--internal-storage)
6. [Scenario 3 — Streaming an MP3 from a URL](#6-scenario-3--streaming-an-mp3-from-a-url)
7. [Scenario 4 — Receiving a Voice Call (Their Voice → Your Speaker)](#7-scenario-4--receiving-a-voice-call-their-voice--your-speaker)
8. [Scenario 5 — Recording Audio with the Mic (Recorder App)](#8-scenario-5--recording-audio-with-the-mic-recorder-app)
9. [Scenario 6 — Full Duplex Voice Call (Your Mic → Network → Their Speaker, and Back)](#9-scenario-6--full-duplex-voice-call-your-mic--network--their-speaker-and-back)
10. [Scenario 7 — Notification Sound + Audio Focus (Ducking)](#10-scenario-7--notification-sound--audio-focus-ducking)
11. [Appendix A — SELinux and Audio](#11-appendix-a--selinux-and-audio)
12. [Appendix B — Audio in AOSP vs OEM](#12-appendix-b--audio-in-aosp-vs-oem)
13. [Appendix C — Useful adb Commands for Audio Debugging](#13-appendix-c--useful-adb-commands-for-audio-debugging)

---

## 1. The Big Picture — Android Audio Architecture

Before diving into individual scenarios, you need a mental model of the entire audio stack. Think of it as a 6-layer sandwich:

```
┌─────────────────────────────────────────────────────────────────────┐
│  Layer 6 — Application Layer                                        │
│  MediaPlayer / ExoPlayer / AudioTrack / AudioRecord / MediaRecorder │
│  Your Kotlin/Java code lives here                                   │
├─────────────────────────────────────────────────────────────────────┤
│  Layer 5 — Java Framework (android.media.*)                         │
│  AudioManager, MediaSession, AudioFocusRequest, MediaRouter         │
│  Lives in frameworks/base/media/java/android/media/                 │
├─────────────────────────────────────────────────────────────────────┤
│  Layer 4 — Native Framework (C++ Services)                          │
│  AudioFlinger, AudioPolicyService, MediaPlayerService               │
│  Lives in frameworks/av/                                            │
├─────────────────────────────────────────────────────────────────────┤
│  Layer 3 — HAL (Hardware Abstraction Layer)                         │
│  audio.primary.default / audio.primary.<device>                     │
│  Lives in hardware/interfaces/audio/ (HIDL/AIDL based)             │
├─────────────────────────────────────────────────────────────────────┤
│  Layer 2 — Kernel / ALSA                                            │
│  tinyalsa / ALSA (Advanced Linux Sound Architecture)               │
│  PCM devices: /dev/snd/pcmC0D0p (playback), pcmC0D0c (capture)    │
├─────────────────────────────────────────────────────────────────────┤
│  Layer 1 — Hardware                                                 │
│  Codec IC, DAC, ADC, Amplifier, Speaker, Microphone, Earpiece       │
└─────────────────────────────────────────────────────────────────────┘
```

Data always flows **top-down for playback** and **bottom-up for capture**.

### The Key Services

| Service | Process | Role |
|---------|---------|------|
| `AudioFlinger` | `audioserver` | The mixer — mixes multiple audio streams into one PCM output |
| `AudioPolicyService` | `audioserver` | The policy engine — decides WHICH output device to use |
| `MediaPlayerService` | `mediaserver` | Orchestrates MediaPlayer (decoding pipeline) |
| `MediaCodecService` | `mediacodec` | Hardware/software codec execution |
| `AudioServer` | `audioserver` | Hosts AudioFlinger + AudioPolicyService |

All these communicate via **Binder IPC** — the same Binder you know from app development.

---

## 2. Terminology Glossary

Before you read the scenarios, internalize these terms. You will see them everywhere.

### Digital Audio Terms

| Term | Meaning |
|------|---------|
| **PCM** | Pulse Code Modulation. Raw, uncompressed audio. A sequence of amplitude samples over time. Everything eventually becomes PCM before hitting the hardware. |
| **Sample Rate** | How many samples are captured/played per second. 44100 Hz = 44,100 samples/sec (CD quality). 48000 Hz = most Android internal processing. |
| **Bit Depth** | Precision of each sample. 16-bit = 65,536 possible amplitude values. 24-bit = 16.7 million. Most Android uses 16-bit internally. |
| **Channels** | Mono = 1 channel, Stereo = 2 channels (left + right). |
| **Frame** | One sample across all channels. Stereo 16-bit = 4 bytes/frame (2 bytes left + 2 bytes right). |
| **Latency** | Time from generating audio data to hearing it from the speaker. Android targets <20ms for interactive audio. |
| **Buffer** | A chunk of audio data held in memory. Larger buffer = more latency but less dropout risk. |
| **Sample Format** | PCM_16_BIT, PCM_8_BIT, PCM_FLOAT, PCM_32_BIT. |
| **Codec** | A coder/decoder. Translates between compressed formats (MP3, AAC) and raw PCM. |
| **Bitrate** | How much data per second in a compressed stream. 128 kbps, 320 kbps. Higher = better quality. |
| **Encoding** | The act of converting PCM → compressed format (MP3/AAC/Opus). |
| **Decoding** | The act of converting compressed format → PCM. |
| **Resampling** | Converting audio from one sample rate to another (e.g., 44100 → 48000 Hz). |
| **Mixing** | Combining multiple PCM streams into one (e.g., your music + notification sound). |

### Android-Specific Terms

| Term | Meaning |
|------|---------|
| **AudioTrack** | Low-level Java API. You feed raw PCM data directly. ExoPlayer uses this internally. |
| **MediaPlayer** | High-level API. Handles file I/O, decoding, playback. You give it a URI. |
| **AudioRecord** | Low-level capture API. You read raw PCM from the mic. |
| **MediaRecorder** | High-level capture API. Handles mic → encode → file. |
| **AudioFlinger** | The core C++ mixer/engine in audioserver. The central bus of Android audio. |
| **AudioPolicyService** | Decides routing: "Play this stream on the earpiece OR speaker OR Bluetooth?" |
| **AudioPolicyManager** | C++ class inside AudioPolicyService that implements routing logic. OEMs replace this. |
| **AudioHAL** | The C/C++ shim between AudioFlinger and the actual kernel driver. |
| **tinyalsa** | A lightweight ALSA library used by the AudioHAL to write to /dev/snd/pcm* |
| **ALSA** | Advanced Linux Sound Architecture. The Linux kernel audio framework. |
| **AudioStream** | A logical audio channel opened by AudioFlinger on the HAL (playback or capture). |
| **AudioPatch** | A connection between an audio source and an audio sink (created by AudioPolicyService). |
| **AudioPort** | An endpoint in an audio patch (e.g., MIC port, SPEAKER port, MIX port). |
| **AudioFocus** | A permission-like mechanism. Only one app "has focus" for a given stream. |
| **AudioAttributes** | Metadata attached to audio: USAGE, CONTENT_TYPE, FLAGS. Used for routing + focus decisions. |
| **AudioStreamType** | STREAM_MUSIC, STREAM_RING, STREAM_NOTIFICATION, STREAM_VOICE_CALL, etc. Controls volume independent. |
| **Volume Stream** | Each stream type has its own volume control. 15 steps typically. |
| **Ducking** | Temporarily lowering one stream's volume because another higher-priority stream is playing. |
| **HIDL** | HAL Interface Definition Language (pre-Android 13). Defines the contract between Android and OEM HAL. |
| **AIDL** | Android Interface Definition Language. Used in newer HAL versions (Android 13+). |
| **AAudio** | Low-latency audio API (Java/NDK). Alternative to AudioTrack. |
| **Oboe** | Google's C++ library that picks AAudio or OpenSL ES based on platform capability. |
| **OpenSL ES** | The older low-latency audio NDK API. Still supported. |
| **MMAP** | Memory-mapped mode. AAudio uses this for ultra-low latency — app directly writes to HW buffer. |
| **Fast Track** | An AudioFlinger mechanism for low-latency playback — bypasses the software mixer. |
| **Normal Track** | The regular AudioFlinger track that goes through full mixing pipeline. |

### Hardware Terms

| Term | Meaning |
|------|---------|
| **DAC** | Digital-to-Analog Converter. Converts PCM digital samples → analog electrical signal → speaker. |
| **ADC** | Analog-to-Digital Converter. Converts analog microphone signal → digital PCM samples. |
| **Codec IC** | An integrated circuit that contains both DAC + ADC + amplifier. Often one chip (e.g., WM8960, AK4497). |
| **I2S** | Inter-IC Sound. A serial bus standard for transferring PCM audio between the SoC and the codec IC. |
| **I2C** | Inter-Integrated Circuit. A control bus used to configure codec registers (volume, mute, routing). |
| **SPI** | Serial Peripheral Interface. Sometimes used for codec control instead of I2C. |
| **PCIe** | Used in some HiFi audio cards, not typical for phones. |
| **Speaker** | Converts electrical analog signal → acoustic pressure waves (sound). |
| **Earpiece** | The small speaker at the top of the phone, used during voice calls. Different driver circuit from main speaker. |
| **Microphone (MEMS)** | Most phones use MEMS (Micro-Electro-Mechanical System) mics. Converts acoustic pressure → electrical signal. |
| **Amplifier** | Boosts the analog signal from the DAC to drive the speaker at audible levels. |
| **Speaker Amplifier IC** | A dedicated chip (e.g., TAS2562, CS35L41) that amplifies the signal. Often has DSP. |
| **Baseband Processor** | A separate processor/modem that handles all cellular communications (calls, data). |
| **DSP** | Digital Signal Processor. Does audio processing: noise cancellation, echo cancellation, EQ, effects. |
| **Mixer (hardware)** | Some codec ICs have a hardware mixer that can combine DAC output + analog sources. |
| **DMA** | Direct Memory Access. Hardware mechanism where the codec/DMA controller reads from RAM directly without CPU. This is how audio data gets to the codec without busy-looping the CPU. |
| **ALSA PCM device** | `/dev/snd/pcmC0D0p` — C=card 0, D=device 0, p=playback. c=capture. |

---

## 3. Hardware Anatomy of an Audio Subsystem

Let's look at the physical hardware chain on a typical Android phone (e.g., Qualcomm Snapdragon + WM8998 codec):

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         SoC (System on Chip)                             │
│                                                                          │
│   CPU Cores        DSP (aDSP/cDSP)        Modem (Baseband)              │
│       │                  │                      │                        │
│   ┌───▼──────────────────▼──────────────────────▼───────────────────┐   │
│   │                   LPASS (Low Power Audio SubSystem)              │   │
│   │                                                                   │   │
│   │   MI2S/I2S/TDM Interfaces   SLIMBUS   PCM Interface             │   │
│   └───────────────────────┬───────────────┬──────────────────────────┘   │
│                           │ I2S (audio    │ I2C (control                 │
│                           │  samples)     │  registers)                  │
└───────────────────────────┼───────────────┼──────────────────────────────┘
                            │               │
                  ┌─────────▼───────────────▼──────────────────────────┐
                  │              Codec IC (e.g., WM8998)                │
                  │                                                      │
                  │   DAC ──→ Headphone Amp ──→ 3.5mm Jack             │
                  │   DAC ──→ Line Out ──→ Speaker Amplifier IC        │
                  │   ADC ←── Mic Bias ←── MEMS Microphone             │
                  │   ADC ←── Earpiece Mic                              │
                  │   Mixer, EQ, ANC DSP                                │
                  └──────────────────────────────────────────────────────┘
                                        │
                    ┌───────────────────┼───────────────────┐
                    ▼                   ▼                   ▼
              Speaker Amp IC      Earpiece Driver      3.5mm Jack
              (e.g. TAS2562)      (internal)           (external HP)
                    │                   │
                  Speaker           Earpiece
              (bottom fire)       (top of phone)
```

### RPi5 Specific (your board)

On the Raspberry Pi 5 running your AOSP build, the audio subsystem is simpler:

```
BCM2712 SoC → I2S → HDMI audio (PCM2767 or similar codec in monitor)
           → 3.5mm Jack (via PCM5102 DAC on some HATs)
           → USB Audio (if USB DAC attached)
```

The RPi5 AOSP audio HAL routes to HDMI by default. There's no built-in earpiece or speaker amplifier.

---

## 4. Scenario 1 — Playing an MP3 Bundled Inside the APK

**Setup:** You have `res/raw/my_song.mp3` in your app. You call `MediaPlayer.create(context, R.raw.my_song)` and `.start()`.

### Step-by-Step Journey

#### Step 1: App Layer — Your Kotlin Code

```kotlin
val mediaPlayer = MediaPlayer.create(context, R.raw.my_song)
mediaPlayer.start()
```

`R.raw.my_song` is an integer resource ID. The system resolves this to a URI like:
`android.resource://com.yourapp/2131951616`

Or you can explicitly use:
```kotlin
val uri = Uri.parse("android.resource://${packageName}/${R.raw.my_song}")
```

Behind the scenes, `MediaPlayer.create()` does several things:
- Creates a `MediaPlayer` Java object
- Calls `native_setup()` — this crosses into C++ via JNI
- The JNI call connects to `MediaPlayerService` via Binder
- Returns an `IMediaPlayer` Binder proxy

#### Step 2: Crossing into Native — JNI and MediaPlayerService

```
Your app process
    │
    │  JNI (android_media_MediaPlayer.cpp)
    ▼
android_media_MediaPlayer_create()
    │
    │  Binder IPC (IMediaPlayerService)
    ▼
MediaPlayerService (in mediaserver process)
    │
    ▼
MediaPlayerService::create() → returns IMediaPlayer stub
```

`MediaPlayerService` is a system service that runs in the `mediaserver` process:
```
/system/bin/mediaserver
```

It creates a `MediaPlayerService::Client` object for your app.

#### Step 3: setDataSource — Reading from APK

When you call `mediaPlayer.prepare()` (or `create()` which calls `prepareAsync()` internally):

1. `MediaPlayer.java` calls `setDataSource(FileDescriptor fd, long offset, long length)`
2. The `AssetManager` opens the APK as a zip file
3. It finds the `res/raw/my_song.mp3` entry and returns a `FileDescriptor` pointing into the APK zip file, with an offset and length
4. This FD + offset + length is passed via Binder to `MediaPlayerService`

> **Why FD passing?** Binder can't pass large blobs of data directly. Instead it passes a file descriptor which the receiving process can `dup()` and use. This is a core Linux/Android pattern.

#### Step 4: Player Selection — NuPlayer

`MediaPlayerService` decides which player implementation to use. For local files, it uses **NuPlayer** (formerly `NuPlayerDriver`). This is the modern media player in AOSP:

```
frameworks/av/media/libmediaplayerservice/nuplayer/
├── NuPlayer.cpp           ← The main engine
├── NuPlayerDriver.cpp     ← Bridges MediaPlayerService to NuPlayer
├── GenericSource.cpp      ← Data source (file, HTTP, etc.)
├── NuPlayerDecoder.cpp    ← Connects to MediaCodec
└── NuPlayerRenderer.cpp  ← Connects to AudioSink (AudioTrack)
```

For your APK MP3:
- `NuPlayer` creates a `GenericSource` pointing to the file descriptor
- `GenericSource` reads the MP3 bytes from the APK zip file

#### Step 5: Demuxing — Splitting Container from Audio

MP3 files are typically just raw audio frames (ID3 tags + MPEG frames). But for formats like MP4/AAC, there's a **container** (MP4 box structure) and the actual audio frames inside.

For MP3:
- `NuPlayer` uses `MPEG2TSExtractor` or `MediaExtractorFactory` to identify and parse the MP3 format
- The extractor identifies: format is MPEG Layer 3, 44100 Hz, stereo, 128 kbps
- It creates a `MediaTrack` that outputs individual MP3 frames

This is handled by:
```
frameworks/av/media/extractors/mp3/MP3Extractor.cpp
```

The MP3 extractor finds MPEG audio frame sync words (`0xFF 0xFB`) and outputs individual compressed frames.

#### Step 6: Decoding — MP3 → PCM

`NuPlayer` sets up a `MediaCodec` to decode the MP3 frames:

```
NuPlayerDecoder → MediaCodec ("audio/mpeg")
                      │
                      ├── Hardware Decoder? (some SoCs have HW MP3 decoder)
                      │   └── via HW codec HAL
                      └── Software Decoder (usually for MP3)
                          └── libFLAC / libstagefright_soft_mp3dec.so
                              └── Uses "pv_mp3dec" or "minimp3" internally
```

The software MP3 decoder:
- Takes compressed MP3 frame bytes as input
- Outputs raw **PCM samples** (16-bit, stereo, 44100 Hz or whatever the file says)
- Each MP3 frame decodes to exactly 1152 PCM samples per channel

**MediaCodec** works with two buffer queues:
```
Input Queue:  [compressed frame] → MediaCodec → Output Queue: [PCM samples]
```

You (or NuPlayer) dequeue input buffers, fill them with compressed data, queue them back. You dequeue output buffers containing decoded PCM.

#### Step 7: NuPlayerRenderer — Getting PCM to AudioTrack

`NuPlayerRenderer` receives decoded PCM from the decoder and writes it to an `AudioTrack`. It does:

1. Opens `AudioTrack` with parameters from the decoded stream (44100 Hz, stereo, PCM_16_BIT)
2. Sets `STREAM_MUSIC` as the stream type
3. Sets `AudioAttributes` with `USAGE_MEDIA` and `CONTENT_TYPE_MUSIC`

```cpp
// Simplified from NuPlayerRenderer.cpp
mAudioSink->open(
    sampleRate,      // 44100
    numChannels,     // 2
    channelMask,     // CHANNEL_OUT_STEREO
    audioFormat,     // AUDIO_FORMAT_PCM_16_BIT
    8,               // buffer count
    mUseOffload ? AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD : AUDIO_OUTPUT_FLAG_NONE,
    ...
);
```

`mAudioSink` here is a `MediaPlayerBase::AudioSink` — an abstraction. In the real world, it's backed by `AudioOutput` which wraps `AudioTrack`.

#### Step 8: AudioTrack — The Gateway to AudioFlinger

`AudioTrack` is the crucial bridge. When you create one:

```cpp
AudioTrack track(
    AUDIO_STREAM_MUSIC,
    44100,
    AUDIO_FORMAT_PCM_16_BIT,
    AUDIO_CHANNEL_OUT_STEREO,
    frameCount,       // buffer size in frames
    AUDIO_OUTPUT_FLAG_NONE
);
```

Internally, `AudioTrack` does this via Binder:
```
AudioTrack (in your process)
    │
    │  Binder IPC (IAudioFlinger)
    ▼
AudioFlinger::createTrack()
    │
    ▼
AudioFlinger creates a PlaybackThread::Track object
    │
    ▼
Returns: IMemory (shared memory region) + IAudioTrack proxy
```

The **shared memory** is critical: AudioFlinger creates a shared memory buffer (via `MemoryDealer` → `ashmem` — Android shared memory, `/dev/ashmem` or `memfd`). Both your app process and AudioFlinger can read/write this buffer directly **without another Binder call per write**. This is how audio writes are fast.

#### Step 9: Writing PCM to the Shared Buffer

NuPlayerRenderer calls `mAudioSink->write(buffer, size)` which calls `AudioTrack::write()`:

```
[PCM data from decoder]
    │
    ▼ (write to shared memory ring buffer)
[AudioTrack shared buffer]  ←──────────── App writes here
    │
    ▼ (AudioFlinger reads from here)
[AudioFlinger reads]
```

AudioFlinger's `PlaybackThread` runs in a real-time thread (SCHED_FIFO priority). It wakes up periodically (every ~5ms for normal tracks, ~2ms for fast tracks) to read from all active tracks' shared buffers.

#### Step 10: AudioFlinger Mixing

This is where the magic happens. `AudioFlinger`'s `MixerThread` has a `AudioMixer` object that:

1. Reads PCM from each active track's shared buffer
2. Resamples if needed (e.g., 44100 → 48000 Hz, because the hardware runs at 48000)
3. Applies volume scaling (software volume control)
4. Mixes all streams together (simple addition with clipping)
5. Applies any effects (EQ, reverb — if configured)

```
Track 1: Music (44100 Hz) ──→ [Resampler] ──→ 48000 Hz PCM ─┐
Track 2: Notification (48000 Hz) ─────────────────────────────┼──→ [Mixer] ──→ Mixed PCM (48000 Hz)
Track 3: System sound (22050 Hz) ──→ [Resampler] ──→ 48000 Hz ┘
```

The resampler in AudioFlinger is located in:
```
frameworks/av/media/libaudioprocessing/AudioResampler.cpp
frameworks/av/media/libaudioprocessing/AudioResamplerSinc.cpp  ← High quality sinc resampler
```

#### Step 11: Effects Processing

If you or the system has effects enabled (EQ, loudness enhancer, visualizer):

```
Mixed PCM → [Effects Chain] → Processed PCM
                │
                ├── EqualizerEffect.cpp
                ├── LoudnessEnhancerEffect.cpp
                ├── VisualizerEffect.cpp  ← This is how Visualizer API works!
                └── BassBoostEffect.cpp
```

Effects are plugins loaded as `.so` files by `EffectsFactory`.

#### Step 12: Writing to HAL — The Kernel Boundary

AudioFlinger's `MixerThread` calls `AudioStreamOut::write()` on the HAL:

```cpp
mOutput->stream->write(mOutput->stream, mMixBuffer, mixBufferSize);
```

This crosses the HAL boundary:
```
AudioFlinger (system process)
    │
    │  HAL API call (direct function pointer, NOT Binder)
    ▼
AudioHAL shared library (audio.primary.default.so or audio.primary.rpi5.so)
    │
    ▼
tinyalsa: pcm_write(pcm, buffer, count)
    │
    ▼
Linux kernel: ALSA PCM device driver
    │
    ▼
/dev/snd/pcmC0D0p  (PCM Card 0, Device 0, Playback)
```

Note: The HAL is called via **direct function pointers** (loaded `.so`), not Binder. This is important for latency — no IPC overhead at the HAL boundary.

> In newer Android (13+) with AudioHAL AIDL, the HAL IS called via Binder. But the HAL process runs at audio priority and uses RT scheduling, so latency is still acceptable.

#### Step 13: DMA — Data Transfer Without CPU

When `pcm_write()` is called in tinyalsa, it does `write()` or `ioctl()` on the `/dev/snd/pcmC0D0p` device node:

```
Kernel ALSA driver receives PCM data
    │
    ▼
Places data into DMA buffer (a region of physical memory)
    │
    ▼
DMA controller (hardware) transfers autonomously:
    DMA Buffer (RAM) → I2S peripheral → Codec IC → DAC → Amplifier → Speaker
        ▲
        │ (DMA generates interrupt when buffer is half-empty → kernel refills → AudioHAL refills → AudioFlinger refills)
```

The DMA runs **in hardware** — no CPU involvement during actual data transfer. The CPU only wakes up (via interrupt) when the buffer needs refilling. This is why audio can play even while the CPU does other work.

#### Step 14: Analog Chain — DAC to Speaker

Inside the codec IC:

```
I2S bus (digital PCM samples, 48000 Hz, 16-bit) arrives at codec chip
    │
    ▼
DAC (Digital-to-Analog Converter) inside codec
    - Converts 16-bit integer samples → analog voltage waveform
    - Uses a sigma-delta or R-2R ladder DAC
    │
    ▼
Analog low-pass filter (removes high-frequency quantization noise)
    │
    ▼
Volume control (analog potentiometer or digital attenuator)
    │
    ▼
Speaker Amplifier IC (e.g., TAS2562)
    - Takes line-level signal (~1V peak)
    - Amplifies to speaker-level (several watts)
    - Class-D amplifier (switches output between +Vcc and GND very fast, ~300kHz)
    │
    ▼
Speaker (voice coil in magnetic field)
    - Electrical current → magnetic force → voice coil moves → cone moves → air pressure waves
    │
    ▼
Sound waves reach your ear → Basilar membrane in cochlea vibrates → Hair cells fire → Brain perceives music!
```

#### Complete Data Flow Diagram

```
APK zip file (MP3 bytes)
    │  [AssetManager reads FD + offset]
    ▼
MP3Extractor (compressed frames)
    │  [parse MPEG sync words]
    ▼
MediaCodec / soft MP3 decoder
    │  [DCT-based decompression → PCM 44100 Hz stereo 16-bit]
    ▼
NuPlayerRenderer
    │  [writes to AudioTrack shared buffer]
    ▼
AudioTrack (app process shared memory)
    │  [ring buffer, ashmem]
    ▼
AudioFlinger MixerThread (audioserver process)
    │  [reads, resamples to 48000 Hz, mixes, applies effects]
    ▼
AudioHAL (audio.primary.*.so)
    │  [HAL write() → tinyalsa pcm_write()]
    ▼
ALSA kernel driver
    │  [DMA buffer → I2S bus]
    ▼
Codec IC DAC
    │  [digital → analog]
    ▼
Speaker Amplifier
    │  [line level → speaker level]
    ▼
Speaker → Sound waves → Your ears
```

---

## 5. Scenario 2 — Playing an MP3 from SD Card / Internal Storage

**Setup:** User has `/sdcard/Music/song.mp3`. Music app calls:
```kotlin
mediaPlayer.setDataSource("/sdcard/Music/song.mp3")
// or
mediaPlayer.setDataSource(context, Uri.fromFile(File("/sdcard/Music/song.mp3")))
```

This scenario is almost identical to Scenario 1, with differences only in **data source** and **file system access**.

### Differences from Scenario 1

#### File Access Layer

```
/sdcard/ is a FUSE filesystem (Android 11+)
    │
    ├── Before Android 11: EXT4 / F2FS on actual storage, mounted at /data/media/
    │
    └── Android 11+: /sdcard → /storage/emulated/0 → FUSE daemon (MediaProvider)
                                                           │
                                                           ▼
                                                    /data/media/0/ (actual EXT4/F2FS)
```

**FUSE (Filesystem in Userspace):** A Linux kernel mechanism where filesystem operations (open, read, write) are handled by a userspace daemon instead of a kernel driver.

In Android 11+, reading `/sdcard/Music/song.mp3`:
1. Your app calls `open("/storage/emulated/0/Music/song.mp3")`
2. Linux VFS layer intercepts it — this path is a FUSE mount
3. The FUSE kernel module forwards the `open` request to the **MediaProvider** process (a system app)
4. MediaProvider checks **scoped storage** permissions — does your app have `READ_MEDIA_AUDIO`?
5. If yes, MediaProvider opens the real file at `/data/media/0/Music/song.mp3` on the EXT4 partition
6. Returns the FD to your app

This means **every read goes through FUSE**, which adds latency. For audio streaming, this is why buffering matters.

#### setDataSource via File Path

```
MediaPlayer.setDataSource(filePath)
    │  JNI
    ▼
android_media_MediaPlayer_setDataSourceAndHeaders() in JNI
    │  Binder → MediaPlayerService
    ▼
MediaPlayerService::Client::setDataSource(url)
    │
    ▼
NuPlayer::GenericSource::setDataSource(url)
    │
    ▼
DataSourceFactory::CreateFromURI()
    │  file:// → FileSource
    ▼
FileSource: wraps open()/read()/lseek() on the file
```

The actual file reading is done by `FileSource` which uses standard POSIX `read()` calls. These go through the FUSE layer as described above.

#### Content URIs (MediaStore)

Modern apps use MediaStore content URIs:
```kotlin
val uri = ContentUris.withAppendedId(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, songId)
mediaPlayer.setDataSource(context, uri)
```

For content URIs:
```
ContentResolver.openFileDescriptor(uri, "r")
    │
    ▼
MediaProvider resolves the URI to real file path
    │
    ▼
Opens the real file
    │
    ▼
Returns ParcelFileDescriptor (FD passed via Binder)
    │
    ▼
MediaPlayer receives this FD and uses setDataSource(FileDescriptor)
```

This is safer than raw paths — MediaStore handles permissions, and the app gets an FD, not the actual path.

#### Everything After That is Identical

Once the MP3 bytes start flowing, the rest of the pipeline (decoding → AudioTrack → AudioFlinger → HAL → speaker) is identical to Scenario 1.

#### Storage Hardware Path (bonus)

Where does `/data/media/` actually live?

```
App reads /sdcard/Music/song.mp3
    │ (FUSE)
    ▼
MediaProvider reads /data/media/0/Music/song.mp3
    │ (EXT4/F2FS filesystem)
    ▼
Block device driver (e.g., UFS, eMMC, SD card controller)
    │
    ▼
Physical NAND flash storage
    │  [NAND controller reads flash cells → page buffer → DMA → RAM]
    ▼
Data arrives in RAM as MP3 bytes
```

For SD card specifically:
```
SD card controller (eMMC/SD host controller) → SDIO bus → SD card NAND flash
```

---

## 6. Scenario 3 — Streaming an MP3 from a URL

**Setup:** Music app plays a stream:
```kotlin
mediaPlayer.setDataSource("https://example.com/stream/song.mp3")
mediaPlayer.prepareAsync()
```

### Key Differences from Local Playback

This adds a **network data source** and changes **prepare** from sync to async (mandatory for network).

#### Step 1: setDataSource with HTTP URL

```
MediaPlayer.setDataSource("https://...")
    │  Binder → MediaPlayerService
    ▼
NuPlayer::GenericSource::setDataSource("https://...")
    │
    ▼
DataSourceFactory::CreateFromURI("https://...")
    │
    ▼
HTTPBase::Make() → NuCachedSource2 wrapping HTTPDataSource
```

Two important sources here:
- **HTTPDataSource**: The actual HTTP fetcher (uses `BpHTTPService` or Android's `MediaHTTPService`)
- **NuCachedSource2**: A caching layer that buffers HTTP data ahead

#### Step 2: MediaHTTPService — The HTTP Client

Android uses a special service for HTTP in media:

```
NuPlayer
    │
    │  Binder
    ▼
MediaHTTPService (runs in mediaserver or app process)
    │
    ▼
Android's HTTP stack:
    ├── HttpURLConnection (Java-based, uses okhttp internally in modern Android)
    └── CronetEngine (if QUIC/HTTP3 capable)
```

The HTTP client:
1. Makes TCP connection to server (port 443 for HTTPS)
2. TLS handshake (for HTTPS)
3. Sends `GET /stream/song.mp3 HTTP/1.1` with `Range: bytes=0-` header
4. Server sends HTTP 200 with MP3 data

**Byte-range requests:** For seekable HTTP sources, NuPlayer uses `Range:` headers. For live streams, it uses `Transfer-Encoding: chunked`.

#### Step 3: NuCachedSource2 — The Buffer

This is a critical component for streaming:

```
HTTP data arrives (network speed varies) → NuCachedSource2 buffer (ring buffer in RAM)
                                                │
                                        NuPlayer reads at decoder speed
                                        (constant rate)
```

The cache has:
- A **low watermark**: if cache drops below this, pause playback and show buffering spinner
- A **high watermark**: target amount to buffer before starting/resuming

This is why you see "buffering..." indicators — it's NuCachedSource2 waiting to fill up.

```cpp
// From NuCachedSource2.cpp
static const size_t kDefaultCacheSize = 192 * 1024;  // 192 KB default
static const size_t kPrefetchLowWaterThreshold = 40 * 1024;
static const size_t kPrefetchHighWaterThreshold = 192 * 1024;
```

#### Step 4: HTTPS/TLS Path

```
App process (MediaHTTPService)
    │
    ▼
BoringSSL (Android's TLS library — Google's fork of OpenSSL)
    │
    ▼
Linux kernel TCP stack (socket)
    │
    ▼
WiFi/Cellular driver
    │
    ├── WiFi: wlan driver → 802.11 frame → WiFi chip (Qualcomm WCN3990, etc.)
    │           → 2.4/5 GHz radio → Access Point → Internet
    │
    └── Cellular: modem driver → Baseband processor → 4G/5G radio → Cell tower → Internet
```

#### Step 5: After Buffering — Same as Scenarios 1 & 2

Once the MP3 bytes are in the NuCachedSource2 cache:
- MP3Extractor parses frames
- MediaCodec decodes to PCM
- NuPlayerRenderer writes to AudioTrack
- AudioFlinger mixes and sends to HAL
- DAC → Speaker

#### Adaptive Streaming (HLS/DASH)

For real music streaming apps (Spotify, YouTube Music), they don't use plain MP3 URLs. They use:

**HLS (HTTP Live Streaming):**
```
Playlist file (.m3u8) → Lists chunk URLs (.ts or .aac segments)
Each chunk = 2-10 seconds of audio in AAC/MP3
Multiple bitrate playlists → Adaptive bitrate switching
```

**DASH (Dynamic Adaptive Streaming over HTTP):**
```
MPD (Media Presentation Description) XML file
Describes segments at multiple bitrates
ExoPlayer dynamically picks bitrate based on network speed
```

ExoPlayer (which Spotify/YouTube use) has its own network stack and buffering logic, but it still ends up writing PCM to AudioTrack, and everything below that is the same.

#### Network Hardware Involved

**WiFi path:**
```
RAM (HTTP data) → TCP/IP stack (kernel) → WiFi driver → PCIe/SDIO bus → WiFi SoC (e.g., Broadcom BCM4359)
→ 802.11 MAC/PHY → Antenna → Radio waves → Router → Internet
```

**Cellular path:**
```
RAM → TCP/IP (kernel) → IPC to Baseband processor (via shared memory or PCIe)
→ Baseband does RLC/MAC/PHY processing → RF transceiver → Antenna → Cell tower
```

Note: On most Android phones, the baseband processor is a completely separate chip with its own CPU, memory, and OS (often a real-time OS). The application processor (where Android runs) communicates with it via a serial interface or shared memory.

---

## 7. Scenario 4 — Receiving a Voice Call (Their Voice → Your Speaker)

**Setup:** Someone calls you. How does their voice come out of your earpiece?

This scenario involves the **telephony stack** and **voice call audio routing** — completely different from media playback.

### The Two Processors Involved

```
┌─────────────────────────────────────────────────────────────────────┐
│  Application Processor (AP)                                         │
│  Runs Android OS, your apps, AudioFlinger                          │
├─────────────────────────────────────────────────────────────────────┤
│  Baseband Processor (BP) / Modem                                    │
│  Runs cellular protocol stack (LTE/5G NR)                          │
│  Has its own CPU, RAM, real-time OS                                 │
│  Handles: Radio Resource Control, RTP, codec (EVS/AMR), encryption │
└─────────────────────────────────────────────────────────────────────┘
```

For voice calls, audio can take TWO paths:

#### Path A: Loopback via AP (Software/VoLTE on AP)

```
Caller's voice → Cell tower → Baseband → [AP processes audio] → Codec IC → Earpiece
```

#### Path B: Hardware Voice Path (Traditional GSM/WCDMA style)

```
Caller's voice → Cell tower → Baseband → [PCM over PCM interface] → Codec IC → Earpiece
                                 ↑
                         AP is NOT in the path at all!
```

Most modern LTE/VoLTE calls use a combination. Let's trace both.

### Sub-Scenario 4A: VoLTE Call (Voice over LTE) — AP-Processed

VoLTE = voice call over the LTE data network using RTP/SIP protocols.

#### Network Side: How Voice Data Arrives

```
Caller speaks → their phone mic → Codec (EVS or AMR-WB encodes voice to 12-60 kbps)
→ RTP packets → LTE packet network → IMS server (P-CSCF, S-CSCF) → your LTE network
→ Cell tower → Baseband processor (modem) on your phone
```

The voice codec used in VoLTE:
- **AMR (Adaptive Multi-Rate):** 4.75 to 12.2 kbps. Narrow band (8000 Hz). Classic 3G voice.
- **AMR-WB (Wideband):** 6.6 to 23.85 kbps. HD Voice (16000 Hz). Much better quality.
- **EVS (Enhanced Voice Services):** Up to 128 kbps, 50 Hz-16 kHz (or even super-wideband). Best quality.

#### Baseband to AP: RIL (Radio Interface Layer)

```
Baseband processor (receives RTP packets with compressed voice frames)
    │
    │  RIL (Radio Interface Layer) — a socket-based protocol
    │  (/dev/socket/rild or shared memory)
    ▼
rild daemon (Radio Interface Layer Daemon) in Android
    │
    ▼
TelephonyService (Java framework)
    │
    ▼
IMS (IP Multimedia Subsystem) stack on Android
```

**RIL** is the bridge between the Android telephony stack and the modem. It's a socket protocol where Android sends AT commands or proprietary commands to the modem, and the modem sends status/data back.

#### IMS Stack on Android

```
android.telephony.ims package (API 28+)
    │
    ▼
ImsService (can be OEM-provided or from modem vendor like Qualcomm's QTI IMS)
    │
    ▼
RTP stack: handles RTP packet processing
    │
    ▼
JitterBuffer: compensates for network jitter (packets arriving out of order or late)
    │
    ▼
Voice Codec Decoder (EVS/AMR → PCM 16000 Hz)
    │
    ▼
Audio processing: AEC (Acoustic Echo Cancellation), NS (Noise Suppression), AGC (Automatic Gain Control)
    │
    ▼
AudioTrack with STREAM_VOICE_CALL, USAGE_VOICE_COMMUNICATION
```

#### AudioPolicyService Routing for Voice Call

When a voice call is active, `AudioPolicyService` is notified:
```
TelephonyManager → AudioManager.setMode(AudioManager.MODE_IN_CALL)
    │
    ▼
AudioPolicyService::setPhoneState(AUDIO_MODE_IN_CALL)
    │
    ▼
AudioPolicyManager re-evaluates all routes:
    - STREAM_VOICE_CALL → EARPIECE (top speaker, small)
    - STREAM_MUSIC → MUTED or directed to earpiece too
    - Bluetooth headset? → Route everything to BT SCO
```

The routing decision:
```
if (connected to Bluetooth headset with SCO capability):
    route voice to BT SCO
else if (wired headset connected):
    route voice to headset earpiece
else:
    route voice to built-in earpiece (small top speaker)
```

**Bluetooth SCO (Synchronous Connection-Oriented):** A dedicated Bluetooth audio connection for voice (8000 Hz or 16000 Hz). Different from A2DP (which is for music, 44100/48000 Hz stereo).

#### Earpiece vs. Speaker

```
Earpiece: A tiny speaker at the top of the phone (~0.8W)
          Used during calls held to your face
          Low volume, directional
          
Main Speaker: Bottom-firing or back speaker (~2-3W)
             Used for speakerphone mode
             Loud, omnidirectional
```

The codec IC routes audio to different outputs:
```
Codec IC DAC → PATH_EARPIECE → Earpiece driver → Earpiece
             → PATH_SPEAKER  → Speaker Amplifier IC → Main Speaker  
             → PATH_HEADPHONE → 3.5mm jack
             → PATH_BT_SCO    → Bluetooth module
```

#### Proximity Sensor — How the Phone Knows to Use Earpiece

When you hold the phone to your face:
```
Proximity Sensor (IR LED + IR detector, top of phone)
    │  Your face reflects IR light back
    ▼
Proximity sensor registers "near" (distance < 5cm)
    │
    ▼
PhoneWindowManager receives PROXIMITY_SCREEN_OFF event
    │
    ▼
Screen turns off (prevents accidental touches with your face)
    │
    ▼
Audio routing stays at earpiece (doesn't switch to speakerphone)
```

When you pull the phone away:
```
Proximity = "far"
    │
    ▼
Screen turns back on
    │
    ▼
Audio stays at earpiece until you explicitly tap Speaker button
```

#### Echo Cancellation — A Critical DSP Step

When you use speakerphone mode during a call, your mic picks up the caller's voice from the speaker. Without echo cancellation, the caller hears their own voice echoed back.

**AEC (Acoustic Echo Cancellation):**
```
Speaker output (reference signal)
    │
    ▼
AEC algorithm models the acoustic path: speaker → room → mic
    │
    ▼
Subtracts estimated echo from mic input
    │
    ▼
Clean mic signal (only your voice, no echo)
```

AEC is computationally expensive. It's often done on a DSP:
- Qualcomm: aDSP (application DSP) or cDSP running ACDB (Audio Calibration Database) and ADSP frameworks
- MediaTek: Audio DSP
- Some codec ICs have built-in AEC DSP

In AOSP, software AEC is in:
```
frameworks/av/media/libeffects/preprocessing/
└── AudioPreProcessing.cpp  ← wraps WebRTC AEC/NS/AGC
```

WebRTC's AEC is the most common — it's the same algorithm used in Chrome/WebRTC video calls.

### Sub-Scenario 4B: 2G/3G Voice Call — Hardware Voice Path

For old WCDMA/GSM calls:

```
Cell tower → Baseband (decodes AMR frames to PCM 8000 Hz)
    │
    │  PCM interface (dedicated serial PCM bus between modem and codec IC)
    ▼
Codec IC receives PCM directly from modem
    │  (AP/Android is completely bypassed)
    ▼
Codec IC DAC → Earpiece
```

This is the traditional path where the Application Processor (Android) is NOT in the audio path at all. The modem talks to the audio codec directly via a hardware PCM bus. This gives very low latency and doesn't drain the AP battery for audio.

When this path is active, AudioFlinger knows via `AudioManager.MODE_IN_CALL` and stops sending audio to the output device (since the hardware is handling it).

---

## 8. Scenario 5 — Recording Audio with the Mic (Recorder App)

**Setup:** User opens a voice recorder app. They tap record.

```kotlin
val recorder = AudioRecord(
    MediaRecorder.AudioSource.MIC,
    44100,
    AudioFormat.CHANNEL_IN_STEREO,
    AudioFormat.ENCODING_PCM_16BIT,
    bufferSize
)
recorder.startRecording()
// loop reading buffers
val bytesRead = recorder.read(buffer, 0, buffer.size)
```

### The Capture Pipeline — Reversed Direction

Audio capture is the mirror image of playback. Data flows from hardware to app.

#### Step 1: Mic Hardware — Acoustic to Digital

```
Sound waves → Microphone
                 │
                 ├── MEMS Microphone (most phones):
                 │   Sound pressure → diaphragm moves → capacitance changes
                 │   → analog electrical signal (~mV level)
                 │
                 └── Some phones have multiple mics:
                     - Main mic: bottom of phone
                     - Secondary mic: top of phone  (for beamforming/noise cancellation)
                     - Ear mic: near earpiece (for VoIP)
```

The MEMS mic outputs a differential analog signal.

#### Step 2: ADC — Analog to Digital

```
Mic analog signal (millivolts)
    │
    ▼
Mic Bias: Codec IC provides a bias voltage (~1.8-2.5V) to power the MEMS mic
    │
    ▼
Mic Amplifier (PGA — Programmable Gain Amplifier): Boosts the weak signal
    │ (gain is controlled by Android via ALSA mixer controls / I2C commands to codec)
    ▼
ADC (Analog-to-Digital Converter):
    - Samples the signal at 48000 Hz (or 44100, or 16000 Hz)
    - Quantizes to 16-bit or 24-bit integer
    - Outputs a stream of PCM samples
    │
    ▼
I2S bus (digital PCM data flows from codec to SoC)
    │
    ▼
LPASS/I2S peripheral in SoC receives data
    │
    ▼
DMA transfer: PCM samples → RAM buffer (in kernel)
```

The DMA works in reverse here: audio data flows FROM the codec IC TO the SoC's RAM, triggered by an interrupt when the buffer is half-full (double-buffering).

#### Step 3: ALSA Kernel Driver — /dev/snd/pcmC0D0c

```
Kernel ALSA driver: DMA fills ring buffer in kernel memory
    │
    ▼
/dev/snd/pcmC0D0c  (PCM Card 0, Device 0, Capture)
```

User space can `read()` from this device node to get PCM samples.

#### Step 4: AudioHAL Capture

```
AudioHAL (audio.primary.*.so)
    │
    ▼
tinyalsa: pcm_read(pcm, buffer, count)
    │  (reads from /dev/snd/pcmC0D0c)
    ▼
Returns PCM samples to AudioFlinger
```

#### Step 5: AudioFlinger RecordThread

```
AudioFlinger has a RecordThread (separate from MixerThread)
    │
    ▼
RecordThread reads from HAL via AudioStreamIn::read()
    │
    ▼
Applies effects (pre-processing effects):
    ├── AEC (if in MODE_IN_COMMUNICATION)
    ├── NS (Noise Suppressor)
    └── AGC (Automatic Gain Control)
    │
    ▼
Writes into the shared memory buffer for the AudioRecord client
    │  (same ashmem shared memory mechanism as playback, reversed direction)
    ▼
AudioRecord in app process reads from shared buffer
```

#### Step 6: AudioRecord — App Reads Buffers

```kotlin
while (isRecording) {
    val bytesRead = recorder.read(buffer, 0, buffer.size)
    // buffer now contains raw PCM 16-bit samples
    processOrSaveBuffer(buffer, bytesRead)
}
```

The `recorder.read()` call:
1. Checks the shared memory ring buffer
2. If enough data available: copies to your `buffer` and returns
3. If not enough: blocks until data is available (or returns SHORT_READ in non-blocking mode)

#### Step 7: Saving to File (MediaRecorder path)

If using `MediaRecorder` instead of `AudioRecord`, the encoding happens automatically:

```
AudioRecord PCM → MediaRecorder internal AudioRecord
    │
    ▼
MediaCodec encoder (e.g., "audio/mp4a-latm" for AAC, "audio/ogg" for Opus)
    │
    ▼
Muxer (MP4Muxer, OggMuxer) adds container headers
    │
    ▼
FileOutputStream writes to /sdcard/recording.m4a
    │  (through FUSE → EXT4 → eMMC/UFS flash)
    ▼
File saved
```

#### Multiple Mic Beamforming (Advanced)

High-end phones with multiple mics use beamforming:

```
Mic 1 (bottom): captures your voice + background noise
Mic 2 (top):   captures mostly background noise (far from your mouth)
    │
    ▼
Beamforming DSP:
    - Phase difference between mic signals reveals direction of sound
    - Algorithm enhances sound from "your mouth" direction
    - Suppresses sound from other directions
    │
    ▼
Clean voice signal with reduced background noise
```

This is done either in the DSP on the codec IC, in the SoC's audio DSP, or in software in the HAL.

#### AudioSource Enum — What Each Means

```kotlin
MediaRecorder.AudioSource.MIC              // Raw mic, minimal processing
MediaRecorder.AudioSource.VOICE_CALL       // Mix of uplink + downlink voice call audio
MediaRecorder.AudioSource.VOICE_DOWNLINK   // Caller's voice only
MediaRecorder.AudioSource.VOICE_UPLINK     // Your voice only (during call)
MediaRecorder.AudioSource.CAMCORDER        // Mic optimized for video recording (different gain, direction)
MediaRecorder.AudioSource.VOICE_COMMUNICATION  // VoIP: AEC + NS applied
MediaRecorder.AudioSource.UNPROCESSED      // Raw ADC output, no enhancements
```

Each AudioSource maps to different ALSA control settings (gain, routing, effect chain) configured in the audio HAL.

#### Permission Flow

```
App requests RECORD_AUDIO permission
    │
    ▼
PackageManager checks: has user granted RECORD_AUDIO?
    │
    ▼
If yes: app can create AudioRecord
    │
    ▼
AudioRecord::createRecord() → Binder → AudioFlinger
    │
    ▼
AudioFlinger checks: does this app have RECORD_AUDIO permission?
    │ (via AppOpsManager, additional runtime check)
    ▼
If yes: creates RecordThread entry, returns shared buffer
```

**AppOps** is a secondary permission system that tracks runtime permission usage. Even if you granted permission, AppOps can revoke it temporarily (e.g., if app is in background and Android 12+ mic indicator is relevant).

---

## 9. Scenario 6 — Full Duplex Voice Call (Your Mic → Network → Their Speaker, and Back)

**Setup:** You make a VoLTE call. Both directions operate simultaneously.

This is the most complex scenario because it requires:
- Capture from mic → encode → transmit
- Receive → decode → playback
- All simultaneously (full duplex)
- With echo cancellation

### Full Architecture Diagram

```
                        YOUR PHONE                                    THEIR PHONE
                   ┌──────────────────┐                         ┌──────────────────┐
                   │  MEMS Mic        │                         │  Speaker/Earpiece │
                   │     ↓            │                         │     ↑            │
                   │  ADC (codec IC)  │                         │  DAC (codec IC)  │
                   │     ↓            │                         │     ↑            │
                   │  AudioFlinger    │                         │  AudioFlinger    │
                   │  RecordThread    │                         │  MixerThread     │
                   │     ↓            │                         │     ↑            │
                   │  AEC / NS / AGC  │                         │  Jitter Buffer   │
                   │     ↓            │                         │     ↑            │
                   │  EVS/AMR Encoder │                         │  EVS/AMR Decoder │
                   │     ↓            │                         │     ↑            │
                   │  RTP Packetizer  │                         │  RTP Depacketizer│
                   │     ↓            │                         │     ↑            │
                   │  SRTP Encrypt    │                         │  SRTP Decrypt    │
                   │     ↓            │                         │     ↑            │
                   │  IMS/SIP Stack   │                         │  IMS/SIP Stack   │
                   │     ↓            │                         │     ↑            │
                   │  LTE Baseband    │────── LTE Network ──────│  LTE Baseband    │
                   └──────────────────┘                         └──────────────────┘
                   ↕ (simultaneously)
                   ┌──────────────────┐
                   │  FULL DUPLEX     │
                   │  Speaker/Earpiece│
                   │     ↑            │
                   │  DAC (codec IC)  │
                   │     ↑            │
                   │  AudioFlinger    │
                   │  MixerThread     │
                   │     ↑            │
                   │  Jitter Buffer   │
                   │     ↑            │
                   │  EVS/AMR Decoder │
                   │  (for their voice│
                   │   coming to you) │
                   └──────────────────┘
```

### Uplink: Your Voice → Network

#### Step 1: Mic Capture
AudioRecord or the IMS app captures from `AudioSource.VOICE_COMMUNICATION`.

#### Step 2: Pre-Processing (Critical for Calls)

```
Raw PCM from mic (48000 or 16000 Hz)
    │
    ▼
AEC (Acoustic Echo Cancellation):
    - Reference signal: what's playing on the speaker/earpiece
    - Subtracts estimated echo from mic signal
    - Without this: caller hears themselves echoed
    │
    ▼
NS (Noise Suppressor / Noise Reduction):
    - Uses spectral subtraction or Wiener filter
    - Estimates background noise floor
    - Reduces steady-state noise (car noise, fan noise)
    │
    ▼
AGC (Automatic Gain Control):
    - Normalizes your voice volume
    - If you're far from mic: boosts gain
    - If you're shouting: reduces gain
    - Ensures consistent volume received by caller
    │
    ▼
Voice Activity Detection (VAD):
    - Detects silence gaps between words
    - During silence: either send comfort noise or suppress packets (DTX)
    - DTX = Discontinuous Transmission: saves bandwidth/battery during silence
```

The WebRTC pre-processing library (`libwebrtc_audio_preprocessing`) provides AEC, NS, and AGC implementations in AOSP:
```
frameworks/av/media/libeffects/preprocessing/
```

#### Step 3: Voice Codec Encoding

```
Clean PCM (16000 Hz, 16-bit, mono) → EVS Encoder
    │
    ├── EVS analyzes 20ms frames at a time (320 samples at 16kHz)
    │
    ├── Uses ACELP (Algebraic Code-Excited Linear Prediction) or TCX (Transform Coded eXcitation)
    │   - Models the vocal tract as a linear filter
    │   - Encodes the excitation signal efficiently
    │
    └── Outputs ~50 frames/second, each 20ms of voice compressed to ~25-48 bytes
        (at 9.6-24 kbps bitrate)
```

**Why 20ms frames?** It's a balance: shorter = lower latency but more overhead per packet. Longer = higher latency but better compression. 20ms is the ITU-T standard.

#### Step 4: RTP Packetization

```
Encoded voice frame (20ms, ~25-48 bytes)
    │
    ▼
RTP (Real-time Transport Protocol) Header added:
    ├── Sequence number (for ordering)
    ├── Timestamp (for jitter buffer sync)
    ├── SSRC (synchronization source identifier)
    └── Payload type (identifies EVS or AMR codec)
    │
    ▼
SRTP (Secure RTP) encryption:
    - DTLS-SRTP key exchange during call setup
    - AES-CM or AES-GCM encryption
    - Message authentication (HMAC-SHA1)
    │
    ▼
UDP datagram → LTE modem → Cell tower → IMS network → Caller's phone
```

**Why UDP, not TCP?** Voice is latency-sensitive. TCP retransmissions would cause noticeable delays. It's better to drop a 20ms frame than to wait for retransmission. Missing frames → brief silence, which the decoder handles with PLC (Packet Loss Concealment).

**PLC (Packet Loss Concealment):** When a packet is lost, the decoder generates synthetic audio (often by extrapolating the last received frame's parameters) instead of outputting silence. This hides packet loss from the listener.

#### Step 5: Network Transport

```
UDP packet → LTE packet network
    │
    ├── QoS (Quality of Service) marking: VoLTE packets get high priority (GBR = Guaranteed Bit Rate bearer)
    │   LTE has dedicated radio bearers for voice that guarantee bandwidth/latency
    │
    ▼
IMS Core Network (P-CSCF → S-CSCF → I-CSCF → other carrier's network)
    │
    ▼
Caller's IMS network → their baseband → their RTP stack
```

### Downlink: Their Voice → Your Ear (simultaneously)

While your voice goes out, their voice comes in on a separate RTP stream.

#### Step 1: Receive RTP Packets

```
UDP packet arrives from network
    │
    ▼
SRTP decryption (symmetric key)
    │
    ▼
RTP header parsing: extract sequence number, timestamp
```

#### Step 2: Jitter Buffer

Network jitter = packets arriving at irregular intervals (even though they were sent at regular 20ms intervals). The jitter buffer smooths this out:

```
Packet 1 arrives at t=0ms
Packet 2 arrives at t=25ms  (5ms late)
Packet 3 arrives at t=15ms  (5ms early — out of order!)
    │
    ▼
Jitter Buffer (adaptive):
    - Reorders out-of-order packets using sequence numbers
    - Holds 2-4 packets (40-80ms) before forwarding to decoder
    - This 40-80ms is the "jitter buffer delay" — contributes to total call latency
    - Adaptive: shrinks if network is stable (lower latency), grows if network is jittery
    │
    ▼
Ordered stream of frames, evenly spaced at 20ms
```

#### Step 3: EVS/AMR Decoding

```
Compressed voice frame (20-48 bytes per 20ms)
    │
    ▼
EVS Decoder:
    - Reconstructs LPC (Linear Predictive Coding) filter coefficients
    - Synthesizes excitation signal
    - Applies synthesis filter → PCM 16000 Hz
    │
    ▼
PCM audio (16000 Hz, 16-bit, mono)
    │
    ▼
Upsampling if needed: 16000 → 48000 Hz (for hardware that requires 48kHz)
```

#### Step 4: Route to Earpiece

```
PCM frames (48000 Hz after resampling)
    │
    ▼
AudioTrack with STREAM_VOICE_CALL / USAGE_VOICE_COMMUNICATION
    │
    ▼
AudioFlinger MixerThread (knows we're in MODE_IN_CALL)
    │
    ▼
AudioPolicyService routes to EARPIECE device
    │
    ▼
AudioHAL configures codec routing: DAC → EARPIECE path
    │
    ▼
ALSA: pcm_write() to earpiece output
    │
    ▼
Codec IC DAC → Earpiece driver → sound
```

### Call Latency Budget

```
Your mouth → mic: ~0ms (speed of sound is irrelevant at this scale)
ADC: ~1ms
Pre-processing (AEC/NS): ~5ms
EVS encoding (one frame): 20ms (frame size)
RTP/SRTP overhead: ~1ms
LTE radio (one-way): 10-30ms
IMS network routing: 5-20ms
Caller's jitter buffer: 40-80ms
Caller's EVS decoding: 5ms
DAC → speaker: ~1ms
    
Total one-way latency: ~80-150ms typical for VoLTE
```

Round-trip (you speak, hear echo): ~160-300ms. ITU-T G.114 recommends <150ms one-way for satisfactory quality.

---

## 10. Scenario 7 — Notification Sound + Audio Focus (Ducking)

**Setup:** You're listening to music. A notification arrives. The music volume drops, the notification plays, then music volume returns. How?

### The Audio Focus System

**Audio Focus** is Android's cooperative audio management system. It's cooperative — apps must respect it (system doesn't force mute, but well-behaved apps respond).

```java
// Music app requests focus when starting playback
AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
    .setAudioAttributes(AudioAttributes.Builder()
        .setUsage(AudioAttributes.USAGE_MEDIA)
        .build())
    .setOnAudioFocusChangeListener(listener)
    .build()
audioManager.requestAudioFocus(focusRequest)
```

#### Audio Focus Types

| Type | Meaning | Music app should... |
|------|---------|---------------------|
| `AUDIOFOCUS_GAIN` | I need focus for a long time (music) | Play at full volume |
| `AUDIOFOCUS_GAIN_TRANSIENT` | I need focus briefly (navigation instruction) | Pause |
| `AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK` | I need focus briefly, others can duck | Duck to 20% volume |
| `AUDIOFOCUS_LOSS` | Someone else took full focus | Stop playback |
| `AUDIOFOCUS_LOSS_TRANSIENT` | Temporary loss (phone call incoming) | Pause |
| `AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK` | Duck requested | Lower volume to 20% |

### Notification Sound Journey

#### Step 1: Notification Posted

```kotlin
// System or app posts notification
NotificationManager.notify(id, notification.build())
```

`NotificationManagerService` receives this (system_server process).

It checks:
- Is DND (Do Not Disturb) mode active? → possibly suppress
- Is the notification channel's sound enabled?
- Is there an assigned notification sound?

If sound should play:
```
NotificationManagerService
    │
    ▼
Ringtone.play() or RingtoneManager.getRingtone(context, notificationSoundUri).play()
    │
    ▼
MediaPlayer or RingtonePlayer (a separate singleton player in system_server or a dedicated process)
```

#### Step 2: RingtonePlayer — System's Audio Player

Android has a dedicated `RingtonePlayer` in `system_server` that handles all system sounds (ringtones, notifications, alarms). This avoids each app needing its own player:

```
system_server process:
    RingtonePlayer
        │
        ▼
    MediaPlayer (internal)
        │
        ▼
    Decodes the notification sound (e.g., /system/media/audio/notifications/Popcorn.ogg)
        │
        ▼
    AudioTrack with AudioAttributes:
        USAGE_NOTIFICATION
        CONTENT_TYPE_SONIFICATION
        STREAM_NOTIFICATION
```

#### Step 3: Notification Requests Audio Focus

```
RingtonePlayer calls:
audioManager.requestAudioFocus(
    ...,
    AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK
)
```

`AudioManager.requestAudioFocus()` goes to:
```
AudioManager (app API)
    │  Binder
    ▼
AudioService (in system_server, NOT audioserver)
    │
    ▼
MediaFocusControl.requestAudioFocus()
    │
    ▼
FocusRequester.handleFocusGainFromRequest()
```

`AudioService` maintains a **focus stack** — a list of focus holders ordered by time:

```
Before notification:
[MusicApp: AUDIOFOCUS_GAIN] ← top (current holder)

After notification requests TRANSIENT_MAY_DUCK:
[NotificationPlayer: AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK] ← top
[MusicApp: AUDIOFOCUS_GAIN] ← below
```

#### Step 4: AudioService Notifies Music App

`AudioService` calls the listener registered by the music app:

```java
// Called on music app's registered listener
onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK)
```

**What the music app SHOULD do (if well-behaved):**
```kotlin
override fun onAudioFocusChange(focusChange: Int) {
    when (focusChange) {
        AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
            mediaPlayer.setVolume(0.2f, 0.2f)  // Duck to 20%
        }
        AudioManager.AUDIOFOCUS_GAIN -> {
            mediaPlayer.setVolume(1.0f, 1.0f)  // Restore
        }
        AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
            mediaPlayer.pause()
        }
    }
}
```

**Auto-ducking (Android 8.0+):** If the music app uses `AudioFocusRequest` with `setWillPauseWhenDucked(false)`, the system can automatically duck without calling the app's listener:

```
AudioService → AudioFlinger: "Duck track belonging to MusicApp"
    │
    ▼
AudioFlinger VolumeShaper: smoothly ramps volume from 1.0 to 0.2 over 0.3 seconds
    │
    ▼
MixerThread applies the ducked volume to music track's samples
    │
    ▼
Music plays at 20% volume
```

**VolumeShaper:** A newer AudioFlinger feature (API 26+) that smoothly transitions volume over time (a ramp), avoiding the abrupt volume cut that was jarring before.

#### Step 5: Mixing Music + Notification

During notification playback, AudioFlinger has TWO active tracks:

```
MixerThread iteration:

Track 1 (Music app, STREAM_MUSIC):
    - PCM samples at 48000 Hz
    - Volume: 0.2 (ducked)
    - Scaled samples: original_sample * 0.2
    
Track 2 (NotificationPlayer, STREAM_NOTIFICATION):
    - PCM samples at 48000 Hz
    - Volume: 1.0 (full)
    - Scaled samples: original_sample * 1.0

Mixer adds them:
    output[i] = (music_sample[i] * 0.2) + (notification_sample[i] * 1.0)
    
    (with clipping: if |output[i]| > 32767, clamp to ±32767)
```

This is why you hear both — they're literally added together in memory before being sent to the DAC.

#### Step 6: Notification Plays → Focus Released

```
Notification sound ends
    │
    ▼
RingtonePlayer.stop()
    │
    ▼
AudioManager.abandonAudioFocus()
    │
    ▼
AudioService.abandonAudioFocus() → pops focus stack
    │
    ▼
New focus stack top: MusicApp with AUDIOFOCUS_GAIN
    │
    ▼
AudioService notifies MusicApp:
onAudioFocusChange(AudioManager.AUDIOFOCUS_GAIN)
    │
    ▼
MusicApp.setVolume(1.0f, 1.0f)  // Restore to full volume
    OR
AudioFlinger VolumeShaper ramps volume back to 1.0 (auto-ducking path)
```

### Volume Streams — Independent Volume Controls

Android maintains separate volume levels for different stream types:

```
STREAM_MUSIC         → Media volume      (0-15 steps, controlled by volume buttons when music is playing)
STREAM_RING          → Ring volume       (0-15 steps)
STREAM_NOTIFICATION  → Notification vol  (0-15 steps, often linked to ring in Android)
STREAM_ALARM         → Alarm volume      (0-15 steps)
STREAM_VOICE_CALL    → Call volume       (0-7 steps)
STREAM_SYSTEM        → System sounds     (0-7 steps)
STREAM_DTMF          → Dial tones        (0-15 steps)
```

These are stored by `AudioService` in `AudioSystem`. When AudioFlinger creates a track, it knows the stream type and applies the current stream volume as a multiplier on top of the track volume.

```
Volume flow:
User presses volume button
    │
    ▼
PhoneWindowManager intercepts KeyEvent.KEYCODE_VOLUME_UP
    │
    ▼
AudioManager.adjustStreamVolume(STREAM_MUSIC, ADJUST_RAISE, SHOW_UI)
    │  Binder
    ▼
AudioService.adjustStreamVolume()
    │
    ▼
AudioSystem.setStreamVolumeIndex(stream, index, device)
    │  (AudioSystem talks to AudioFlinger via libaudioclient)
    ▼
AudioFlinger updates the volume for all tracks with that stream type
    │
    ▼
MixerThread applies new volume on next mix iteration
    │
    ▼
Immediately audible volume change (no codec register change needed — it's all digital multiplication)
```

### Do Not Disturb (DND) and Ringer Mode

```
AudioManager.RINGER_MODE_NORMAL   → All sounds allowed
AudioManager.RINGER_MODE_VIBRATE  → Sounds suppressed, vibrator motor activated
AudioManager.RINGER_MODE_SILENT   → All sounds suppressed, no vibration
```

DND (Do Not Disturb):
```
NotificationManagerService checks DND policy before playing notification sound
    │
    ├── DND priority mode: only allow certain callers/apps
    ├── DND total silence: suppress everything including media
    └── DND alarms only: suppress notifications but allow alarms
```

The **Vibrator service** is separate from audio but related for notifications:
```
VibratorService → writes to /sys/class/leds/vibrator/activate (or /dev/vibrator)
                → Vibrator motor (LRA — Linear Resonant Actuator, or ERM — Eccentric Rotating Mass)
                → Haptic feedback
```

### DuckingShaper — How Volume Ramping Works Internally

```cpp
// Simplified VolumeShaper in AudioFlinger
class VolumeShaper {
    float computeVolume(int64_t framePosition) {
        // Linear ramp from startVolume to endVolume over rampFrames
        float t = (float)(framePosition - startFrame) / rampFrames;
        t = clamp(t, 0.0f, 1.0f);
        return startVolume + t * (endVolume - startVolume);
    }
};
```

This ensures smooth ducking — no clicks or pops from abrupt volume changes. The ramp is typically 0.3 seconds (about 14,400 frames at 48kHz).

---

## 11. Appendix A — SELinux and Audio

SELinux enforces which processes can access audio devices. This is very relevant for AOSP OEM development.

### Audio-Related SELinux Domains

| Domain | Process | Audio Access |
|--------|---------|-------------|
| `audioserver` | `/system/bin/audioserver` | Full HAL access, `/dev/snd/*` |
| `mediaserver` | `/system/bin/mediaserver` | Socket to audioserver, no direct HAL |
| `system_server` | System services | AudioService (Java), no HAL |
| `untrusted_app` | Your app | Only via Binder to audioserver |
| `hal_audio_default` | OEM audio HAL | Granted snd device access |

### Key SELinux Rules for Audio

```
# Allow audioserver to access sound devices
allow audioserver snd_device:chr_file { read write open ioctl };

# Allow audioserver to access ALSA nodes
allow audioserver audio_device:chr_file rw_file_perms;

# Allow app to connect to audioserver via Binder
allow untrusted_app audioserver:binder { call transfer };
```

If your OEM audio HAL is having trouble accessing `/dev/snd/*`:
```bash
# Check denials
adb logcat -d | grep "avc: denied" | grep snd

# Check labels
adb shell ls -laZ /dev/snd/

# Expected:
# u:object_r:audio_device:s0 /dev/snd/pcmC0D0p
```

---

## 12. Appendix B — Audio in AOSP vs OEM

### What OEMs Replace

| Component | AOSP Default | OEM Replacement |
|-----------|-------------|----------------|
| AudioHAL | `audio.primary.default` | `audio.primary.msm8998` (Qualcomm) |
| AudioPolicyManager | AOSP default | OEM custom routing rules |
| Audio Effects | AOSP effects | OEM effects (Dolby Atmos, Harman/Kardon) |
| Voice pre-processing | WebRTC AEC | OEM DSP (aDSP, QCOM FastRTP) |
| Codec IC driver | N/A (kernel level) | OEM kernel driver |
| ACDB/UCM | N/A | Qualcomm ACDB (Audio Calibration DB) |

### audio_policy_configuration.xml

This XML file (in the HAL package) defines all available audio devices and how they connect:

```xml
<!-- Example: vendor/myoem/hal/audio/audio_policy_configuration.xml -->
<audioPolicyConfiguration version="1.0">
  <modules>
    <module name="primary" halVersion="3.0">
      <mixPorts>
        <mixPort name="primary output" role="source" flags="AUDIO_OUTPUT_FLAG_PRIMARY">
          <profile name="" format="AUDIO_FORMAT_PCM_16_BIT"
                   samplingRates="48000" channelMasks="AUDIO_CHANNEL_OUT_STEREO"/>
        </mixPort>
      </mixPorts>
      <devicePorts>
        <devicePort tagName="Speaker" type="AUDIO_DEVICE_OUT_SPEAKER" role="sink">
        </devicePort>
        <devicePort tagName="Built-In Mic" type="AUDIO_DEVICE_IN_BUILTIN_MIC" role="source">
        </devicePort>
      </devicePorts>
      <routes>
        <route type="mix" sink="Speaker" sources="primary output"/>
        <route type="mix" sink="primary input" sources="Built-In Mic"/>
      </routes>
    </module>
  </modules>
</audioPolicyConfiguration>
```

This is what tells `AudioPolicyManager` which physical device is connected to which mixer port.

### UCM (Use Case Manager) — tinyalsa Routing

For complex codec IC routing (multiple paths, multiple scenarios), the HAL uses ALSA UCM files:

```
/vendor/etc/sound/
├── RPi5/
│   ├── RPi5.conf          ← UCM configuration
│   └── HiFi.conf          ← Use case: HiFi playback
```

UCM maps high-level use cases (MEDIA_PLAYBACK, VOICE_CALL, VOIP) to specific ALSA mixer controls on the codec:

```
# HiFi.conf (simplified)
SectionDevice."Speaker" {
    Comment "Speaker"
    EnableSequence [
        cdev "hw:0"
        cset "name='Speaker Volume' 80"
        cset "name='Speaker Switch' on"
        cset "name='DAC1 Left Mixer DACL1 Switch' 1"
    ]
    DisableSequence [
        cset "name='Speaker Switch' off"
    ]
}
```

These ALSA mixer controls are I2C register writes to the codec IC, configuring internal routing within the codec.

---

## 13. Appendix C — Useful adb Commands for Audio Debugging

### AudioFlinger State

```bash
# Dump complete AudioFlinger state: all active tracks, threads, volumes
adb shell dumpsys media.audio_flinger

# Dump AudioPolicyService: active policies, routing, devices
adb shell dumpsys media.audio_policy

# Dump AudioService (Java layer): focus stack, volumes, ringer mode
adb shell dumpsys audio
```

### Active Tracks and Routing

```bash
# See what's playing and on which device
adb shell dumpsys media.audio_flinger | grep -A5 "Output thread"

# Check current output device
adb shell dumpsys audio | grep "Output devices"

# Check audio mode
adb shell dumpsys audio | grep "Audio mode"
```

### ALSA Devices

```bash
# List ALSA PCM devices
adb shell cat /proc/asound/pcm

# List ALSA cards
adb shell cat /proc/asound/cards

# Check ALSA mixer controls (codec IC register values)
adb shell tinymix       # if tinymix is on device

# Check codec state
adb shell cat /proc/asound/card0/codec#0
```

### Logcat for Audio

```bash
# Audio-related logs
adb logcat -s AudioFlinger AudioPolicyManager AudioTrack AudioRecord

# HAL-level logs
adb logcat -s audio_hw_primary

# MediaPlayer/NuPlayer logs
adb logcat -s NuPlayer GenericSource NuPlayerDecoder

# Telephony audio
adb logcat -s ImsCallSession TelephonyManager RIL
```

### Audio Focus Debugging

```bash
# See current audio focus stack
adb shell dumpsys audio | grep -A20 "Audio Focus stack"

# Watch focus changes in real-time
adb logcat -s AudioFocus MediaFocusControl
```

### Volume Debugging

```bash
# See all stream volumes
adb shell dumpsys audio | grep "Stream volume"

# Force set volume (for testing)
adb shell media volume --stream 3 --set 8   # stream 3 = STREAM_MUSIC, volume index 8
```

### PCM Capture (Record what's being sent to hardware)

```bash
# Capture 5 seconds of what's playing (pcm dump)
adb shell tinycap /sdcard/capture.wav -D 0 -d 0 -c 2 -r 48000 -b 16 -p 1024 -n 4
adb pull /sdcard/capture.wav

# Play a test tone
adb shell tinyplay /sdcard/test.wav -D 0 -d 0
```

### SELinux Audio Denials

```bash
# See all audio-related SELinux denials
adb logcat -d | grep "avc: denied" | grep -E "snd|audio|media"

# Check context of audio nodes
adb shell ls -laZ /dev/snd/
adb shell ls -laZ /sys/class/sound/
```

---

## Summary — The Key Insight

After reading all 7 scenarios, the pattern becomes clear:

```
Everything eventually becomes PCM.
Everything eventually goes through AudioFlinger.
AudioFlinger is the central hub of Android audio.
```

Whether it's:
- An MP3 in your APK → decoded to PCM → AudioFlinger
- A WAV on SD card → read as raw PCM → AudioFlinger
- A streaming MP3 from URL → decoded to PCM → AudioFlinger
- A VoLTE call → AMR/EVS decoded to PCM → AudioFlinger
- A notification beep → PCM loaded from file → AudioFlinger

They ALL end up as PCM samples in AudioFlinger's `MixerThread`, get mixed together, then flow through the HAL, through the kernel, through the DMA, through the DAC, through the amplifier, and finally as air pressure waves into your ear.

The audio system is one of the most beautifully layered parts of Android — every layer has a clear job, and the abstraction boundaries (HAL, AudioTrack shared memory, AIDL) allow OEMs to customize their specific codec IC and routing without touching the framework above.

---

*Journal written for: Android developer (5+ yrs app experience) learning AOSP internals.*
*AOSP version reference: android-15.0.0_r14 (Android 15)*
*Last updated: 2026-04*
