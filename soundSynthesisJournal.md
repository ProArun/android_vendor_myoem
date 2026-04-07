# Sound Synthesis in Android: A Complete Deep-Dive Journal

> **Prerequisite:** Read `audioJournal.md` first. You already know that everything becomes PCM and goes through AudioFlinger.
> This journal answers: **where does that PCM come from when there is no audio file?**
> Audience: 5+ year Android developer learning AOSP internals + audio DSP from scratch.

---

## Table of Contents

1. [The Fundamental Question — What Is Sound?](#1-the-fundamental-question--what-is-sound)
2. [Anatomy of a Sound — ADSR, Timbre, Harmonics](#2-anatomy-of-a-sound--adsr-timbre-harmonics)
3. [Oscillators — The Source of All Synthesis](#3-oscillators--the-source-of-all-synthesis)
4. [Types of Synthesis — The Big Picture](#4-types-of-synthesis--the-big-picture)
5. [Additive Synthesis — Building Sound from Sine Waves](#5-additive-synthesis--building-sound-from-sine-waves)
6. [Subtractive Synthesis — Filtering a Rich Source](#6-subtractive-synthesis--filtering-a-rich-source)
7. [FM Synthesis — Frequency Modulation](#7-fm-synthesis--frequency-modulation)
8. [Wavetable Synthesis — The Heart of Modern Apps](#8-wavetable-synthesis--the-heart-of-modern-apps)
9. [Sample-Based Synthesis — The Piano App Engine](#9-sample-based-synthesis--the-piano-app-engine)
10. [Granular Synthesis](#10-granular-synthesis)
11. [Physical Modeling Synthesis](#11-physical-modeling-synthesis)
12. [How a Piano App Works — Full Journey](#12-how-a-piano-app-works--full-journey)
13. [How a Game App Produces Sound — Full Journey](#13-how-a-game-app-produces-sound--full-journey)
14. [MIDI in Android — From Key Press to PCM](#14-midi-in-android--from-key-press-to-pcm)
15. [Android Audio APIs for Synthesis — Choosing the Right One](#15-android-audio-apis-for-synthesis--choosing-the-right-one)
16. [Writing a Synthesizer in Android — Code-Level Walkthrough](#16-writing-a-synthesizer-in-android--code-level-walkthrough)
17. [Game Audio Engines — FMOD, Wwise, OpenAL](#17-game-audio-engines--fmod-wwise-openal)
18. [Effects DSP — Reverb, Chorus, Delay](#18-effects-dsp--reverb-chorus-delay)
19. [Latency — Why It Matters for Instruments](#19-latency--why-it-matters-for-instruments)
20. [Appendix — Mathematics of Audio DSP](#20-appendix--mathematics-of-audio-dsp)

---

## 1. The Fundamental Question — What Is Sound?

Before synthesis, you must deeply understand what you are synthesizing.

Sound is a **longitudinal pressure wave** propagating through air. When a speaker cone moves forward, it compresses air molecules. When it moves back, it rarefies them. This creates alternating regions of compression and rarefaction traveling at ~343 m/s (the speed of sound at 20°C).

Your ear's basilar membrane (inside the cochlea) is a frequency analyzer — different positions along it resonate at different frequencies. High frequencies resonate near the base, low frequencies near the apex. Hair cells at each position convert mechanical vibration into electrical nerve impulses. Your brain interprets this frequency map as **pitch**.

### Key Properties of Sound Waves

```
                 ← one cycle →
Amplitude
  │   ╭──╮               ╭──╮
  │  ╭╯  ╰╮             ╭╯  ╰╮
──┼─╭╯     ╰╮───────────╭╯     ╰╮──── Time
  │╭╯        ╰╮         ╭╯        ╰╮
  ╯           ╰╮       ╭╯
              ╰───────╯

Amplitude  → How loud (determines volume/dB SPL)
Frequency  → How many cycles per second (Hz) — determines pitch
Period     → Duration of one cycle = 1/frequency
Phase      → Where in the cycle we are (0° to 360°)
Waveform   → The shape of the wave — determines timbre (tone quality)
```

### Frequency and Musical Notes

The equal-tempered musical scale divides each octave into 12 semitones. Each semitone is a ratio of **2^(1/12) ≈ 1.05946**.

```
Middle C = C4 = 261.63 Hz
A4 (concert pitch) = 440.00 Hz (the note orchestras tune to)
A3 = 220.00 Hz  (one octave below = half the frequency)
A5 = 880.00 Hz  (one octave above = double the frequency)

MIDI note number formula:
  frequency = 440 * 2^((noteNumber - 69) / 12)
  
  MIDI 60 = C4 = 440 * 2^((60-69)/12) = 261.63 Hz
  MIDI 69 = A4 = 440 * 2^((69-69)/12) = 440.00 Hz
  MIDI 81 = A5 = 440 * 2^((81-69)/12) = 880.00 Hz
```

---

## 2. Anatomy of a Sound — ADSR, Timbre, Harmonics

### ADSR Envelope

Every real-world sound has a **time evolution** — it doesn't just switch on at full volume. This evolution is modeled by an **ADSR envelope**:

```
Volume
  │                ╭──────────────╮
  │              ╭╯ (Sustain)     ╰───╮
  │            ╭╯                    ╰──╮
  │          ╭╯                         ╰──╮
  │        ╭╯                              ╰──╮
  │      ╭╯                                   ╰──╮
  │────╭╯                                         ╰────
  │   /│\         │             │    │                │
  │  / │ \        │             │    │                │
  │ /  │  \___────┤             ├────┤                │
  ┼────┼───────────┼─────────────┼────┼────────────────┼─ Time
  0   A    D       S(hold)      R(release begins)    end
      │    │       │            │
   Attack Decay Sustain      Release
```

| Phase | Description | Piano example | Synth pad example |
|-------|-------------|---------------|-------------------|
| **Attack** | Time to reach peak volume from 0 | Very short (<5ms): "hammer hits string" | Slow (500ms): "warm fade in" |
| **Decay** | Time to fall from peak to sustain level | Short (50ms): initial "thwack" diminishes | Medium (200ms) |
| **Sustain** | Volume level held while note is held | Piano: level drops over time (strings ring) | Constant (0.8) |
| **Release** | Time to fade from sustain to 0 after key released | Medium (500ms): strings damped but ring briefly | Long (2s): pad fades out |

Without ADSR, all notes would sound like instant on/off square steps — robotic and unnatural.

### Harmonics and Timbre

Why does a piano and a guitar playing the same note (440 Hz) sound completely different? Both produce 440 Hz. The difference is in the **harmonic content** (timbre/tone color).

When a string vibrates at its fundamental frequency (440 Hz), it also vibrates simultaneously at multiples:
- **Fundamental (1st harmonic):** 440 Hz
- **2nd harmonic (1st overtone):** 880 Hz
- **3rd harmonic:** 1320 Hz
- **4th harmonic:** 1760 Hz
- **nth harmonic:** n × 440 Hz

```
Piano A4 (440Hz) Harmonic Content (approximate dB levels):
├── 440 Hz  ████████████████████████████████  0 dB (strongest)
├── 880 Hz  ███████████████████               -8 dB
├── 1320 Hz ██████████████                   -12 dB
├── 1760 Hz █████████████                    -14 dB
├── 2200 Hz █████████                        -18 dB
└── ...      getting quieter

Guitar A4 (440Hz) Harmonic Content:
├── 440 Hz  ████████████████████████████████  0 dB
├── 880 Hz  ████████████████████████          -3 dB  (stronger 2nd harmonic vs piano)
├── 1320 Hz ██████████████████████            -5 dB  (stronger 3rd harmonic)
├── 1760 Hz ████████████████                 -9 dB
└── ...
```

The **relative amplitudes of harmonics** is what makes each instrument unique. Timbre = harmonic fingerprint.

**Inharmonics:** Some instruments (bells, xylophones) have overtones that are NOT exact multiples of the fundamental. These inharmonic partials create the "metallic" or "bell-like" quality. Physical modeling synthesis must reproduce these.

### The Fourier Theorem — The Key to Synthesis

**Joseph Fourier proved:** Any periodic waveform can be decomposed into a sum of sine waves at different frequencies and amplitudes.

This means:
- Any sound = sum of sine waves (Fourier decomposition)
- If you can generate the right sine waves and sum them → you synthesize any sound (Fourier synthesis = additive synthesis)

This mathematical fact is the entire theoretical basis for sound synthesis.

---

## 3. Oscillators — The Source of All Synthesis

An **oscillator** generates a periodic waveform at a specific frequency. In digital audio, this means generating a sequence of sample values that repeat at the desired frequency.

### Basic Waveforms

#### Sine Wave
```
     ╭──╮           ╭──╮
    ╯    ╰         ╯    ╰
───╯       ╰─────╯       ╰───
                 ╰──╯
```
- Purest sound: contains ONLY the fundamental frequency, no harmonics
- Sounds: flute (approximation), test tone, sub-bass
- Formula: `y(t) = A * sin(2π * f * t)`
- PCM generation: `sample[n] = amplitude * sin(2π * frequency * n / sampleRate)`

#### Square Wave
```
    ┌───┐   ┌───┐   ┌───┐
    │   │   │   │   │   │
────┘   └───┘   └───┘   └────
```
- Contains: fundamental + all ODD harmonics (1st, 3rd, 5th, 7th...)
  - Amplitudes: 1, 1/3, 1/5, 1/7...
- Sounds: clarinet-like (clarinets are closed-pipe instruments, produce odd harmonics), chiptune
- 50% duty cycle = perfect square. Changing duty cycle = pulse wave (PWM synthesis)

#### Sawtooth Wave
```
  /|  /|  /|
 / | / | / |
/  |/  |/  |
```
- Contains: fundamental + ALL harmonics (1st, 2nd, 3rd, 4th...)
  - Amplitudes: 1, 1/2, 1/3, 1/4...
- Richest waveform — most harmonics
- Sounds: brass-like, strings (via subtractive synthesis), classic synthesizer bass
- Most useful starting point for subtractive synthesis

#### Triangle Wave
```
  /\    /\
 /  \  /  \
/    \/    \
```
- Contains: fundamental + odd harmonics only, but with much lower amplitude than square
  - Amplitudes: 1, 1/9, 1/25, 1/49... (falls as 1/n²)
- Softer than square — fewer high harmonics
- Sounds: mellow, flute-like with more harmonics than sine, Gameboy-era chiptune

#### Noise (White Noise)
- Random samples uniformly distributed: all frequencies simultaneously
- Useful for: hi-hat, snare drum body, wind effects, ocean waves

```kotlin
// White noise in code
val sample = (Random.nextFloat() * 2f - 1f) * amplitude
```

### Digital Oscillator Implementation

```kotlin
class SineOscillator(private val sampleRate: Int) {
    private var phase = 0.0   // Current phase (0.0 to 1.0)
    
    fun generate(frequency: Float, amplitude: Float, buffer: FloatArray) {
        val phaseIncrement = frequency / sampleRate  // Phase advance per sample
        for (i in buffer.indices) {
            buffer[i] = (amplitude * sin(2.0 * PI * phase)).toFloat()
            phase += phaseIncrement
            if (phase >= 1.0) phase -= 1.0  // Wrap around
        }
    }
}
```

The `phaseIncrement = frequency / sampleRate` relationship is fundamental:
- At 48000 Hz sample rate, for A4 (440 Hz): increment = 440/48000 ≈ 0.009167
- This means the phase advances 0.9167% of a full cycle per sample
- After 48000/440 ≈ 109.09 samples, we complete one full cycle

### Aliasing — The Enemy of Digital Oscillators

When generating a sawtooth wave digitally with many harmonics, harmonics above the **Nyquist frequency** (sampleRate/2 = 24000 Hz at 48kHz) cannot be represented and "fold back" into the audible range as **alias frequencies**.

```
Sawtooth at 10000 Hz has harmonics at:
10000, 20000, 30000, 40000... Hz

At 48000 Hz sample rate, Nyquist = 24000 Hz.
30000 Hz → folds to 48000 - 30000 = 18000 Hz (alias!)
40000 Hz → folds to 48000 - 40000 = 8000 Hz (alias!)

These aliases are wrong frequencies — they sound "buzzy" or "metallic" in wrong ways.
```

**Solutions:**
- **Band-Limited Oscillators (BLITs/BLEPs):** Generate only harmonics below Nyquist
- **PolyBLEP:** A technique to correct the discontinuities at waveform edges in real-time — most common in soft synths
- **Wavetable oscillators:** Pre-compute band-limited waveforms for different frequency ranges (this is exactly what wavetable synthesis does!)

---

## 4. Types of Synthesis — The Big Picture

```
SYNTHESIS TYPES
│
├── Oscillator-based (generate sound from scratch)
│   ├── Additive Synthesis    → Sum of sine waves
│   ├── Subtractive Synthesis → Rich oscillator + filters
│   ├── FM Synthesis          → Frequency modulation of oscillators
│   ├── Wavetable Synthesis   → Lookup table oscillator (THIS IS THE BIG ONE)
│   ├── Phase Distortion      → Casio CZ synths
│   └── Granular Synthesis    → Tiny grains of sound
│
├── Sample-based (playback and manipulation of recorded audio)
│   ├── Sample Playback       → Trigger pre-recorded samples (SoundPool, AudioTrack)
│   ├── Sample-based Synth    → Multi-sampled instrument (piano apps)
│   └── Granular Sample Play  → Granular manipulation of samples
│
└── Physical Modeling (simulate the physics of instruments)
    ├── Karplus-Strong        → Plucked string simulation
    ├── Digital Waveguide     → Acoustic resonator simulation
    └── Modal Synthesis       → Resonant modes of objects
```

---

## 5. Additive Synthesis — Building Sound from Sine Waves

**Concept:** Since any sound = sum of sine waves (Fourier theorem), if you add enough sine oscillators at the right frequencies and amplitudes, you can synthesize any sound.

### Example: Synthesizing a Clarinet Tone

A clarinet produces odd harmonics:
```
fundamental:  330 Hz  at amplitude 1.00
3rd harmonic: 990 Hz  at amplitude 0.50
5th harmonic: 1650 Hz at amplitude 0.25
7th harmonic: 2310 Hz at amplitude 0.12
...
```

In code:
```kotlin
fun synthesizeClarinet(frequency: Float, sampleRate: Int, buffer: FloatArray) {
    val harmonics = listOf(
        Pair(1, 1.00f),   // fundamental
        Pair(3, 0.50f),   // 3rd harmonic
        Pair(5, 0.25f),   // 5th harmonic
        Pair(7, 0.12f),   // 7th harmonic
        Pair(9, 0.06f),   // 9th harmonic
    )
    
    for (i in buffer.indices) {
        var sample = 0f
        val t = i.toDouble() / sampleRate
        for ((harmonicNumber, amplitude) in harmonics) {
            sample += amplitude * sin(2.0 * PI * frequency * harmonicNumber * t).toFloat()
        }
        buffer[i] = sample / harmonics.size  // normalize
    }
}
```

### Problems with Additive Synthesis

1. **CPU expensive:** A realistic piano has ~100 harmonics per note. 88 keys = 8800 oscillators just for one chord!
2. **Hard to control:** Changing timbre requires adjusting many oscillator amplitudes independently
3. **Cannot reproduce inharmonic sounds easily**

**Where additive synthesis IS used:**
- Hammond organ simulators (each tonewheel = one harmonic = one oscillator)
- The Roland VP-330 vocoder
- Kawai's early additive synths
- Modern "spectral synthesis" in academic audio tools

---

## 6. Subtractive Synthesis — Filtering a Rich Source

**Concept:** Start with a harmonically rich waveform (sawtooth or square — already contains many harmonics), then use **filters** to remove (subtract) unwanted frequencies.

```
[Oscillator: Sawtooth] → [Filter: Low Pass Filter] → [Amplifier: ADSR] → Output
      rich in harmonics      cuts high frequencies     shapes volume
```

This is the basis of:
- Classic Moog synthesizers
- Roland SH-series (SH-101, SH-2000)
- ARP Odyssey
- Most classic analog synths
- Most "virtual analog" soft synths

### Filters — The Core of Subtractive Synthesis

A **filter** is a frequency-selective amplifier: it boosts or cuts certain frequency ranges.

#### Filter Types

```
Low Pass Filter (LPF):
  Amplitude │
            │████████████
            │            ╲
            │             ╲___________
            └──────────────────────── Frequency
                         ↑
                   Cutoff frequency
  Passes: low frequencies. Cuts: high frequencies.
  Use: remove harsh harmonics, make sound "warmer", classic bass synth sound

High Pass Filter (HPF):
  Amplitude │
            │            ████████████
            │          ╱
            │___________
            └──────────────────────── Frequency
  Passes: high frequencies. Cuts: low frequencies.
  Use: remove "muddy" bass from a sound, make it "thin" or "airy"

Band Pass Filter (BPF):
  Amplitude │
            │        ╭────╮
            │       ╱     ╲
            │______╱       ╲__________
            └──────────────────────── Frequency
  Passes: only frequencies near the center frequency.
  Use: wah-wah pedal effect, telephone voice effect

Notch Filter (Band Reject):
  Amplitude │
            │████████      ████████████
            │        ╲   ╱
            │         ╰─╯
            └──────────────────────── Frequency
  Cuts: a narrow band of frequencies.
  Use: remove hum (60Hz notch), comb filtering effects
```

#### Key Filter Parameters

| Parameter | Description | Effect |
|-----------|-------------|--------|
| **Cutoff Frequency (Fc)** | The frequency where the filter "cuts" | Higher Fc = brighter sound |
| **Resonance (Q)** | How much the filter emphasizes Fc | High Q = "whistling" peak at cutoff → classic synth sound |
| **Filter Slope** | How steeply it cuts (dB/octave) | 12 dB/oct (2-pole), 24 dB/oct (4-pole Moog ladder) |

The **Moog Ladder Filter** (24 dB/oct, 4-pole) is one of the most famous filter designs — it produces a characteristic "fat" sound when resonance is pushed.

#### Filter Implementation (Simple 1-pole LPF)

```kotlin
class LowPassFilter {
    private var previousOutput = 0f
    
    // Compute cutoff coefficient from frequency
    fun setCutoff(cutoffHz: Float, sampleRate: Int) {
        val rc = 1f / (2f * PI.toFloat() * cutoffHz)
        val dt = 1f / sampleRate
        alpha = dt / (rc + dt)  // alpha: 0 = fully closed, 1 = fully open
    }
    
    var alpha = 0.5f
    
    fun process(input: Float): Float {
        // Simple RC (resistor-capacitor) filter equation
        // output = prev_output + alpha * (input - prev_output)
        previousOutput += alpha * (input - previousOutput)
        return previousOutput
    }
}
```

A real synthesizer uses a much more sophisticated filter (Biquad IIR, State Variable Filter, Moog ladder model), but the principle is the same: the filter state (`previousOutput`) creates frequency-dependent behavior.

#### Cutoff Envelope (Filter Sweep)

One of the most iconic sounds in synthesis: the filter cutoff is modulated by an ADSR envelope:

```
Filter cutoff starts LOW (muffled)
    │ key pressed
    ▼
Attack: cutoff quickly rises to peak (bright "TWANG")
    │
    ▼
Decay: cutoff falls back to sustain level ("settles")
    │
    ▼
Sustain: cutoff stays at sustain level (held tone)
    │ key released
    ▼
Release: cutoff falls to minimum (muffled trail)
```

This produces the classic "synth bass" sound — the bright initial attack followed by a darker sustain.

---

## 7. FM Synthesis — Frequency Modulation

**Concept:** The frequency of one oscillator (the **carrier**) is varied (modulated) by the output of another oscillator (the **modulator**). This creates complex sidebands — new frequencies appear that weren't in either original oscillator.

```
[Modulator Oscillator] ──→ (modulates frequency of) ──→ [Carrier Oscillator] → Output
```

### The Mathematics

```
Carrier alone: y(t) = A * sin(2π * fc * t)

With FM:        y(t) = A * sin(2π * fc * t + I * sin(2π * fm * t))
                                               │
                                               └── Modulator output scaled by Index (I)
```

Where:
- **fc** = carrier frequency
- **fm** = modulator frequency  
- **I** = modulation index (depth of modulation)

**Modulation Index (I)** is everything in FM synthesis:
- I = 0: pure carrier, no sidebands, simple tone
- I = 0.5: subtle harmonics added, slightly richer
- I = 2: many sidebands, complex timbre
- I = 10: very complex, metallic, dissonant
- I changes over time (via envelope) → timbre evolves = the signature FM sound

### Sidebands

When a carrier at 440 Hz is modulated by a modulator at 110 Hz:

```
New frequencies appear at:
  fc ± n*fm  for n = 1, 2, 3, ...
  
440 ± 110 = 330 Hz, 550 Hz
440 ± 220 = 220 Hz, 660 Hz
440 ± 330 = 110 Hz, 770 Hz
...

So a simple 2-operator FM creates a rich harmonic series!
```

### Yamaha DX7 — The Famous FM Synth

The DX7 (1983) has 6 operators (oscillators) that can be connected in 32 different **algorithms** (configurations of who modulates whom). It produced sounds impossible with subtractive synthesis — electric pianos, bells, brass, metallic hits.

```
DX7 Algorithm 5 (simplified):
  Op6 → Op5     Op4 → Op3 → Op2 → Op1
          │                         │
          └─────────────────────────┘→ Output

(Two independent FM chains added together)
```

Android app **DX7 simulation:** Apps like "Dexed" implement all 32 DX7 algorithms in software, computing millions of FM operations per second per note.

### FM Synthesis in Android Code (2-Operator)

```kotlin
class FMSynthesizer(private val sampleRate: Int) {
    var carrierFreq = 440f
    var modulatorRatio = 2f    // modulator freq = carrier * ratio
    var modulationIndex = 3f   // how deep the modulation is
    
    private var carrierPhase = 0.0
    private var modulatorPhase = 0.0
    
    fun generateSamples(buffer: FloatArray, amplitude: Float) {
        val modulatorFreq = carrierFreq * modulatorRatio
        val carrierIncrement = carrierFreq / sampleRate
        val modulatorIncrement = modulatorFreq / sampleRate
        
        for (i in buffer.indices) {
            // Compute modulator output
            val modOutput = sin(2.0 * PI * modulatorPhase)
            
            // Use modulator to shift carrier phase
            val carrierInput = 2.0 * PI * carrierPhase + modulationIndex * modOutput
            
            // Compute final sample
            buffer[i] = (amplitude * sin(carrierInput)).toFloat()
            
            // Advance phases
            carrierPhase += carrierIncrement
            modulatorPhase += modulatorIncrement
            if (carrierPhase >= 1.0) carrierPhase -= 1.0
            if (modulatorPhase >= 1.0) modulatorPhase -= 1.0
        }
    }
}
```

---

## 8. Wavetable Synthesis — The Heart of Modern Apps

This is the most important synthesis technique for modern apps. Understand this deeply.

### The Problem Wavetable Solves

Computing `sin(2π * f * t)` for every sample using the `Math.sin()` function is slow:
- Math.sin() involves a Taylor series expansion — many multiplications
- At 48000 Hz sample rate, you need 48000 sin() calls per second per oscillator
- A polyphonic synthesizer with 32 voices = 1,536,000 sin() calls per second
- With FM, multiple operators per voice: potentially 10M+ transcendental function calls/second

**Wavetable synthesis replaces runtime computation with table lookup.**

### What is a Wavetable?

A **wavetable** is simply a pre-computed array of samples representing exactly **one cycle** of a waveform.

```kotlin
// A wavetable for a sine wave with 2048 samples per cycle
val WAVETABLE_SIZE = 2048
val sineWavetable = FloatArray(WAVETABLE_SIZE) { index ->
    sin(2.0 * PI * index / WAVETABLE_SIZE).toFloat()
}
```

```
sineWavetable[0]    = sin(0°)     = 0.000
sineWavetable[1]    = sin(0.17°)  = 0.003
sineWavetable[512]  = sin(90°)    = 1.000
sineWavetable[1024] = sin(180°)   = 0.000
sineWavetable[1536] = sin(270°)   = -1.000
sineWavetable[2047] = sin(359.8°) = -0.003
```

The table stores the waveform at 2048 evenly-spaced phase points. To look up the value at any phase:
```kotlin
fun getWavetableSample(phase: Double): Float {
    // phase is 0.0 to 1.0 (normalized phase)
    val index = (phase * WAVETABLE_SIZE).toInt() % WAVETABLE_SIZE
    return sineWavetable[index]
}
```

This replaces `sin()` (expensive) with an array access (very cheap).

### Wavetable Oscillator — Playing at Any Frequency

```
Wavetable (2048 samples of one cycle)

To play at frequency F with sample rate SR:
  Phase increment per sample = F / SR * WAVETABLE_SIZE
  
Example: A4 (440 Hz) at 48000 Hz, table size 2048:
  Phase increment = 440 / 48000 * 2048 = 18.773 samples per audio sample

Per audio sample:
  1. Look up wavetable[currentTableIndex]
  2. currentTableIndex += 18.773
  3. Wrap around if >= 2048
  4. Output the sample
```

```kotlin
class WavetableOscillator(
    private val wavetable: FloatArray,
    private val sampleRate: Int
) {
    private var tableIndex = 0.0  // floating-point index into table
    private val tableSize = wavetable.size
    
    fun setFrequency(frequency: Float) {
        phaseIncrement = frequency / sampleRate * tableSize
    }
    
    private var phaseIncrement = 0.0
    
    fun getNextSample(): Float {
        // Integer index for lookup
        val index = tableIndex.toInt() % tableSize
        val nextIndex = (index + 1) % tableSize
        
        // Linear interpolation for smooth output
        val fraction = tableIndex - tableIndex.toLong()
        val sample = wavetable[index] + fraction.toFloat() * (wavetable[nextIndex] - wavetable[index])
        
        // Advance
        tableIndex += phaseIncrement
        if (tableIndex >= tableSize) tableIndex -= tableSize
        
        return sample
    }
}
```

### Linear Interpolation — Why It Matters

When the phase increment is not an integer (it almost never is), the "true" value lies between two table entries. Without interpolation, you get **quantization artifacts** (stepping between table values = high-frequency noise).

```
Table:  [0.0, 0.3, 0.6, 0.9, 1.0, ...]
         ↑         ↑
         index=0   index=2, but we want index=1.5

Linear interpolation:
  fraction = 0.5 (halfway between index 1 and 2)
  value = table[1] + 0.5 * (table[2] - table[1])
        = 0.3 + 0.5 * (0.6 - 0.3) = 0.45  ✓ (correct midpoint)
```

Higher quality synthesizers use **cubic interpolation** (uses 4 surrounding points) or **sinc interpolation** (mathematically perfect, but expensive).

### The Aliasing Problem in Wavetable Synthesis

Remember aliasing from Section 3? A sawtooth wavetable pre-computed with all its harmonics will alias when played at high frequencies.

**Example:**
- A sawtooth wavetable at 2048 samples contains harmonics up to 1024 (Nyquist for the table)
- Playing this wavetable at 1000 Hz: harmonics at 1000, 2000, 3000... Hz
- At 48000 Hz sample rate, Nyquist = 24000 Hz
- 25th harmonic: 25000 Hz → aliases to 48000-25000 = 23000 Hz ← audible alias!
- Playing the same wavetable at 10000 Hz: ONLY the fundamental fits below Nyquist. 2nd harmonic (20000 Hz) barely fits, 3rd (30000 Hz) aliases badly.

### Multi-Wavetable (Band-Limited Wavetables) — The Solution

Professional synthesizers store MULTIPLE wavetables for the same waveform, each pre-filtered to only contain harmonics that won't alias at a given frequency range.

```
For a sawtooth wave:

Wavetable for 20-40 Hz range:    Contains harmonics 1 through 600 (all fit below Nyquist)
Wavetable for 40-80 Hz range:    Contains harmonics 1 through 300
Wavetable for 80-160 Hz range:   Contains harmonics 1 through 150
Wavetable for 160-320 Hz range:  Contains harmonics 1 through 75
Wavetable for 320-640 Hz range:  Contains harmonics 1 through 37
Wavetable for 640-1280 Hz range: Contains harmonics 1 through 18
Wavetable for 1280+ Hz range:    Only fundamental (no harmonics needed/possible)
```

At runtime, the oscillator selects the appropriate band-limited wavetable for the current playback frequency:

```kotlin
fun selectWavetable(frequency: Float): FloatArray {
    return when {
        frequency < 40f   -> sawtoothTable_20_40
        frequency < 80f   -> sawtoothTable_40_80
        frequency < 160f  -> sawtoothTable_80_160
        frequency < 320f  -> sawtoothTable_160_320
        frequency < 640f  -> sawtoothTable_320_640
        frequency < 1280f -> sawtoothTable_640_1280
        else              -> sawtoothTable_pure  // near sine
    }
}
```

This is computationally very cheap (a single array access per sample after table selection) and produces alias-free output.

### Wavetable Morphing (Advanced)

The word "wavetable" in the context of modern synthesizers (Waldorf Blofeld, PPG Wave, Serum, Vital) often means something more: a **collection of different waveforms** that the oscillator can smoothly cross-fade between:

```
Wavetable (64 frames, each frame is one cycle of a different waveform):
  Frame 0:  Pure sine
  Frame 8:  Slightly distorted sine
  Frame 16: Triangle-ish
  Frame 32: Sawtoothish
  Frame 48: Complex, rich harmonic content
  Frame 63: Wild, complex wave

Wavetable position control:
  Position = 0.0 → uses Frame 0 (pure sine)
  Position = 0.5 → uses Frame 32 (sawtooth-ish), interpolated between 31 and 32
  Position = 1.0 → uses Frame 63 (complex)

LFO or envelope modulates Position over time → waveform morphs = dynamic timbre!
```

This is how Serum (the popular VST synth) and its Android equivalents work. Each "wavetable" is a 2D array:
```kotlin
// wavetables[frameIndex][sampleIndex]
val wavetables = Array(64) { FloatArray(2048) }
```

---

## 9. Sample-Based Synthesis — The Piano App Engine

**Concept:** Instead of synthesizing sound mathematically, record the actual instrument at multiple pitches and velocities, then play back the appropriate sample, transposing it as needed.

This is how **every realistic instrument app** works: Grand Piano, Drum Machine, Orchestra libraries.

### Multi-Sampling — The Recording Process

Recording a realistic piano for an app requires:

```
88 keys × multiple velocities × multiple mic positions = thousands of samples

Typical piano library:
├── Every 3rd key recorded (others are pitch-shifted from nearest neighbor)
├── 4-8 velocity layers:
│   ├── pp  (pianissimo, very soft)   → recorded at ~30% velocity
│   ├── p   (piano, soft)             → recorded at ~45% velocity
│   ├── mp  (mezzo-piano)             → recorded at ~60% velocity
│   ├── mf  (mezzo-forte)             → recorded at ~70% velocity
│   ├── f   (forte, loud)             → recorded at ~80% velocity
│   └── ff  (fortissimo, very loud)   → recorded at ~95% velocity
├── Sustain pedal on/off variants
├── Release samples (the sound when a key is released)
└── Pedal noise, key click samples
```

Why velocity layers? Piano tone quality changes drastically with velocity, not just volume:
- Soft note: warm, few high harmonics, long sustain
- Hard note: bright, many high harmonics, different attack character
- A simple volume scale cannot reproduce this — you need separate recordings.

### Pitch Shifting — Playing One Sample at Multiple Pitches

A piano with 88 keys sampled every 3 semitones has recordings for 30 notes. The other 58 notes are produced by **pitch-shifting** the nearest sample:

```
Recorded: C4 (261.63 Hz)

To play C#4 (277.18 Hz) from the C4 sample:
  Playback speed = 277.18 / 261.63 = 1.05946 (= 2^(1/12) = one semitone up)
  Play the sample at 1.05946x normal speed
```

**Pitch shifting by changing playback speed** (also called sample rate conversion in this context):

```
If we recorded C4 at 48000 Hz and play at 1.05946x speed:
  We advance through the sample 1.05946 samples per output sample.
  This is exactly the same as the wavetable oscillator with fractional phase increment!
```

```kotlin
class SamplePlayer(
    private val sample: FloatArray,    // PCM samples of recorded C4
    private val sampleSampleRate: Int  // sample rate the sample was recorded at
) {
    private var playbackIndex = 0.0
    
    fun setOutputFrequency(targetHz: Float, fundamentalHz: Float) {
        // ratio = how much faster to play = target / recorded_fundamental
        val ratio = targetHz / fundamentalHz
        // advance this many recorded samples per output sample
        playbackIndexIncrement = ratio
    }
    
    private var playbackIndexIncrement = 1.0
    
    fun getNextSample(): Float {
        if (playbackIndex >= sample.size) return 0f  // sample ended
        
        val index = playbackIndex.toInt()
        val fraction = (playbackIndex - index).toFloat()
        val a = sample[index]
        val b = if (index + 1 < sample.size) sample[index + 1] else 0f
        
        playbackIndex += playbackIndexIncrement
        return a + fraction * (b - a)  // linear interpolation
    }
}
```

### Looping — Making Short Samples Play Longer

A piano key held for 10 seconds generates a 10-second audio file per note per velocity. 88 × 6 × 10 = 5280 seconds = ~500 MB for just piano.

Instead, samples are **looped** at their sustain portion:

```
Sample structure:
[Attack portion: 0-200ms][Sustain portion: 200ms-3000ms][Release: 3000ms-3500ms]
                              ↑                    ↑
                           LoopStart            LoopEnd
                              │←───────loop─────→│

Playback:
1. Play attack (0 → LoopStart) normally
2. When reaching LoopEnd, jump back to LoopStart
3. Repeat until key released
4. When key released, play Release portion (crossfade + tail)
```

**Loop crossfade:** To avoid a "click" when jumping from LoopEnd back to LoopStart, the audio is crossfaded:
- A few milliseconds before LoopEnd: start fading in the LoopStart audio
- Blend smoothly between the end and start
- Requires the waveform amplitudes to match at both loop points

### The SFZ Format — How Sample Libraries Are Organized

`.sfz` (SoundFont Zipped or Sample Format Zero) is a text-based format describing how samples are mapped to MIDI notes and velocities:

```sfz
<global>
ampeg_release=0.8

<group>
// Keys from C2 to B2, velocity 1-63 (soft playing)
key=C2 lovel=1 hivel=63 sample=piano_C2_soft.wav loopstart=4800 loopend=48000
key=D#2 lovel=1 hivel=63 sample=piano_Ds2_soft.wav loopstart=4800 loopend=48000
...

<group>
// Same keys, velocity 64-127 (loud playing)
key=C2 lovel=64 hivel=127 sample=piano_C2_loud.wav loopstart=4800 loopend=48000
...
```

The app's SFZ parser reads this to build a lookup table: `(midiNote, velocity) → SamplePlayer`.

### SoundFont2 (.sf2) — The Other Format

SF2 (SoundFont 2) is a binary format (developed by E-mu Systems / Creative Labs) widely used in:
- General MIDI synthesizers
- Android's built-in MIDI synthesizer (Sonivox)
- Piano apps

SF2 structure:
```
SF2 File
├── INFO chunk: metadata
├── sdta chunk (Sample Data):
│   ├── smpl: raw 16-bit PCM samples (all samples concatenated)
│   └── sm24: optional 24-bit extension
└── pdta chunk (Preset Data):
    ├── phdr: Preset headers (name, bank, preset number)
    ├── pbag: Preset index list
    ├── pmod: Preset modulators
    ├── pgen: Preset generators (parameter values)
    ├── inst: Instrument headers
    ├── ibag: Instrument index list
    ├── imod: Instrument modulators
    ├── igen: Instrument generators (keyRange, velRange, sampleRef, tuning, filter, envelope...)
    └── shdr: Sample headers (name, start, end, loopStart, loopEnd, sampleRate, originalPitch)
```

**Generators** in SF2 are parameters that control how a sample is played: pitch offset, volume, filter cutoff, ADSR envelope values, modulation, chorus/reverb amount.

Android's **Sonivox** synthesizer reads SF2 files:
```
frameworks/base/packages/MtpDocumentsProvider/ (not here)
external/sonivox/  ← the SF2 synthesizer
```

---

## 10. Granular Synthesis

**Concept:** Any sound is broken into tiny "grains" (5-100ms). Grains are individually pitch-shifted, time-stretched, spatially positioned, and overlapped to create a new texture.

```
Original sample:
[─────────────────────────────────────────────────────]
        ↓ extract grains (overlapping)
Grain 1: [═════]
Grain 2:   [═════]
Grain 3:     [═════]
Grain 4:       [═════]
        ↓ reorder, pitch shift, scatter grains
Output: [═══][══][═════][═══][══][══][════]  ← different texture!
```

### Granular Applications

- **Time stretching without pitch change:** Extract grains faster than normal → more grains per second → slower playback without pitch shift. (This is what Ableton, Audacity use)
- **Pitch shift without speed change:** Change grain playback rate but advance through source at normal rate
- **Cloud textures:** Scatter grains randomly → ambient clouds of sound
- **Granular scrubbing:** Playback head position = grain source position → "scrub" through a sound

### Granular Synthesis Android Example

```kotlin
class GranularPlayer(
    private val source: FloatArray,
    private val sampleRate: Int
) {
    data class Grain(
        var sourcePosition: Float,    // position in source sample
        var playbackPosition: Float,  // position within this grain
        var grainLength: Int,         // in samples
        var pitchRatio: Float,        // playback speed (1.0 = original pitch)
        var amplitude: Float,
        var isActive: Boolean = true
    )
    
    private val activeGrains = mutableListOf<Grain>()
    private var sourceReadHead = 0f
    
    fun triggerGrain(
        sourcePosNormalized: Float,  // 0.0 to 1.0
        grainSizeMs: Float,
        pitchRatio: Float,
        amplitude: Float
    ) {
        activeGrains.add(Grain(
            sourcePosition = sourcePosNormalized * source.size,
            playbackPosition = 0f,
            grainLength = (grainSizeMs / 1000f * sampleRate).toInt(),
            pitchRatio = pitchRatio,
            amplitude = amplitude
        ))
    }
    
    fun process(output: FloatArray) {
        output.fill(0f)
        val deadGrains = mutableListOf<Grain>()
        
        for (grain in activeGrains) {
            for (i in output.indices) {
                if (grain.playbackPosition >= grain.grainLength) {
                    grain.isActive = false
                    break
                }
                
                // Hanning window envelope for smooth grain edges
                val windowPos = grain.playbackPosition / grain.grainLength
                val envelope = (0.5f * (1f - cos(2f * PI.toFloat() * windowPos)))
                
                // Get sample from source
                val srcIdx = (grain.sourcePosition + grain.playbackPosition * grain.pitchRatio).toInt()
                val sample = if (srcIdx < source.size) source[srcIdx] else 0f
                
                output[i] += sample * envelope * grain.amplitude
                grain.playbackPosition += 1f
            }
            if (!grain.isActive) deadGrains.add(grain)
        }
        activeGrains.removeAll(deadGrains)
    }
}
```

The **Hanning window** (cosine bell shape) applied to each grain prevents clicks at grain boundaries. Overlapping windowed grains produces smooth, continuous output.

---

## 11. Physical Modeling Synthesis

**Concept:** Simulate the actual physics of how an instrument produces sound. Instead of looking up samples or computing oscillator equations, solve differential equations that model the physical system.

### Karplus-Strong Algorithm — Plucked String

This is the simplest physical model — and produces a remarkably realistic plucked string sound with almost no computation.

**Physical intuition:**
1. Pluck a string: inject a burst of energy (noise)
2. The wave reflects between bridge and nut
3. Each reflection, slightly loses energy (damping)
4. Higher harmonics decay faster (due to string stiffness)

**Algorithm:**
1. Fill a circular buffer with a burst of white noise (= the "pluck")
2. Each sample output = average of current and previous buffer output (= simple low-pass filter = damping)
3. Feed the output back into the buffer (= resonance = string vibration)

```kotlin
class KarplusStrongString(
    frequency: Float,
    sampleRate: Int,
    damping: Float = 0.996f  // 0.99 = more damping (dull), 0.999 = less damping (bright)
) {
    // Buffer size = samples per cycle = wavelength
    private val bufferSize = (sampleRate / frequency).toInt()
    private val buffer = FloatArray(bufferSize) { Random.nextFloat() * 2f - 1f }  // white noise init
    private var writeIndex = 0
    private val dampingFactor = damping
    
    fun getNextSample(): Float {
        val readIndex = (writeIndex + 1) % bufferSize
        
        // Low-pass filter: average current and next sample in buffer
        val newSample = dampingFactor * 0.5f * (buffer[writeIndex] + buffer[readIndex])
        
        buffer[writeIndex] = newSample
        writeIndex = (writeIndex + 1) % bufferSize
        
        return newSample
    }
}
```

**Why does this work?**
- Buffer size = frequency (pitch of the note)
- The feedback loop → sustained oscillation (string keeps ringing)
- The averaging → low-pass filter → higher harmonics decay faster (realistic)
- Initial noise → realistic attack with harmonic content (not a pure tone)

**Realistic extensions:**
- Stretch tuning: add fractional delay (allpass filter) for exact pitch
- Stiffness: add dispersion filter (higher harmonics flat → inharmonic, like piano high strings)
- Excitation: real guitar pluck model instead of noise burst
- Body resonance: convolution with impulse response of guitar body

### Digital Waveguide Synthesis

A more complete physical model used in high-quality instruments:

```
[Excitation] → [Delay Line Right-going] → [Bridge] → Output
                       ↑                       |
                       └── [Delay Line Left-going] ←┘
```

Two delay lines simulate right-going and left-going waves on the string. Filters at the terminations (nut, bridge) model the reflection characteristics. Used in:
- Julius O. Smith's waveguide models (Stanford CCRMA)
- Roland's V-Piano
- Yamaha's PHYSICAL MODELING (VL1 synthesizer)

### Modal Synthesis

Models an object as a set of resonant modes (like a bell with multiple frequencies):

```
Impulse (hammer hit)
    │
    ├─→ Mode 1 resonator (200 Hz, fast decay) → partial output 1
    ├─→ Mode 2 resonator (412 Hz, medium decay) → partial output 2
    ├─→ Mode 3 resonator (683 Hz, slow decay) → partial output 3
    └─→ Mode N resonator ...
    
Sum of all resonators → realistic bell/glass/marimba sound
```

Each resonator is a simple second-order IIR filter tuned to the mode frequency with the appropriate decay rate.

---

## 12. How a Piano App Works — Full Journey

**Setup:** User taps a key on a piano app's touch keyboard. Sound is produced.

### Complete Architecture of a Piano App

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Piano App Process                                                        │
│                                                                          │
│  Touch Event (MotionEvent from Choreographer)                           │
│       │                                                                  │
│       ▼                                                                  │
│  Key Detection: which key was touched? (UI coordinate → note mapping)   │
│       │                                                                  │
│       ▼                                                                  │
│  Velocity Calculation: how fast was the tap? (touch pressure/speed→vel) │
│       │                                                                  │
│       ▼                                                                  │
│  SampleEngine (C++ via JNI / Kotlin)                                    │
│       │                                                                  │
│  ┌────▼───────────────────────────────────────────────────────────┐     │
│  │ Voice Manager                                                    │     │
│  │   - Look up: (midiNote=60, velocity=90) → C4_medium.wav        │     │
│  │   - Find a free Voice slot (polyphony limit: 32 voices)         │     │
│  │   - Initialize Voice: SamplePlayer + ADSR + Filter             │     │
│  └────────────────────────────────────────────────────────────────┘     │
│       │                                                                  │
│  ┌────▼───────────────────────────────────────────────────────────┐     │
│  │ Audio Callback (called by AudioTrack / Oboe every ~5ms)         │     │
│  │                                                                  │     │
│  │   output_buffer.fill(0f)                                         │     │
│  │   for each active Voice:                                         │     │
│  │       voice.render(output_buffer)  // SamplePlayer+ADSR+Filter │     │
│  │   limiter.process(output_buffer)   // prevent clipping          │     │
│  └────────────────────────────────────────────────────────────────┘     │
│       │                                                                  │
│       ▼                                                                  │
│  AudioTrack.write() or Oboe callback buffer fill                        │
└─────────────────────────────────────────────────────────────────────────┘
       │  (PCM data)
       ▼
AudioFlinger → HAL → DAC → Speaker
```

### Step 1: Touch → Note Detection

```kotlin
class PianoKeyboardView : View(context) {
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val x = event.x
        val y = event.y
        
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val key = detectKey(x, y)       // which piano key geometrically
                val velocity = calculateVelocity(event)  // 0-127
                noteOn(key.midiNote, velocity)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val key = detectKey(x, y)
                noteOff(key.midiNote)
            }
        }
        return true
    }
    
    private fun calculateVelocity(event: MotionEvent): Int {
        // Some apps use touch pressure: event.pressure * 127
        // Others use touch speed: how fast the finger moved to hit the key
        // Simple: use constant or touch area
        return (event.pressure * 127).toInt().coerceIn(1, 127)
    }
}
```

### Step 2: Note On → Voice Allocation

```kotlin
class SampleEngine {
    private val voices = Array(MAX_POLYPHONY) { Voice() }
    private val sampleMap = HashMap<Pair<Int,Int>, FloatArray>()  // (note, velLayer) → PCM data
    
    fun noteOn(midiNote: Int, velocity: Int) {
        // 1. Find appropriate sample
        val velocityLayer = velocityToLayer(velocity)  // 0-5 (6 layers)
        // Find nearest sampled note (piano sampled every 3 semitones)
        val sampledNote = nearestSampledNote(midiNote)
        val sampleData = sampleMap[Pair(sampledNote, velocityLayer)]!!
        
        // 2. Calculate pitch ratio
        val semitoneOffset = midiNote - sampledNote
        val pitchRatio = 2.0.pow(semitoneOffset / 12.0).toFloat()
        
        // 3. Allocate a voice
        val voice = findFreeVoice() ?: stealVoice()  // voice stealing if all busy
        voice.init(
            sample = sampleData,
            pitchRatio = pitchRatio,
            amplitude = velocityToAmplitude(velocity),
            loopStart = 22050,      // from SFZ/SF2 metadata
            loopEnd = 96000,
            attackSamples = 48,     // 1ms attack
            decaySamples = 2400,    // 50ms decay
            sustainLevel = 0.8f,
            releaseSamples = 24000  // 500ms release
        )
        voice.state = VoiceState.PLAYING
    }
    
    fun noteOff(midiNote: Int) {
        // Find voice playing this note
        val voice = voices.firstOrNull { it.midiNote == midiNote && it.state == VoiceState.PLAYING }
        voice?.startRelease()   // Trigger release phase of ADSR
    }
}
```

### Step 3: Voice Stealing

When all polyphony slots are filled and a new note is triggered:

```kotlin
private fun stealVoice(): Voice {
    // Strategy options:
    
    // 1. Steal quietest voice
    return voices.minByOrNull { it.currentAmplitude }!!
    
    // 2. Steal oldest voice
    return voices.minByOrNull { it.startTime }!!
    
    // 3. Steal voice in release phase (already fading out)
    return voices.firstOrNull { it.state == VoiceState.RELEASING }
        ?: voices.minByOrNull { it.currentAmplitude }!!  // fallback to quietest
}
```

Voice stealing is an art — poorly done it causes audible "popping" as notes are cut.

### Step 4: The Audio Callback — Rendering All Voices

This is the **real-time heart** of the piano app. It runs in a high-priority real-time thread:

```kotlin
// Called by Oboe/AudioTrack every ~5ms (240 frames at 48kHz)
override fun onAudioReady(audioStream: AudioStream, audioData: FloatArray, numFrames: Int): DataCallbackResult {
    audioData.fill(0f)
    
    for (voice in voices) {
        if (voice.state == VoiceState.IDLE) continue
        
        // Render this voice into a temporary buffer
        voice.render(tempBuffer, numFrames)
        
        // Accumulate into output (mixing)
        for (i in 0 until numFrames * 2) {  // *2 for stereo
            audioData[i] += tempBuffer[i]
        }
    }
    
    // Apply master limiter to prevent clipping
    limiter.process(audioData, numFrames)
    
    return DataCallbackResult.Continue
}
```

### Step 5: Voice Rendering — ADSR + Sample Playback

```kotlin
class Voice {
    fun render(buffer: FloatArray, numFrames: Int) {
        for (frame in 0 until numFrames) {
            // 1. Get sample from SamplePlayer (with pitch ratio)
            val rawSample = samplePlayer.getNextSample()
            
            // 2. Apply ADSR envelope
            val envelopeGain = adsr.getNextSample()
            
            // 3. Apply optional filter (if filter cutoff is modulated)
            val filteredSample = filter.process(rawSample * envelopeGain)
            
            // 4. Apply velocity-based amplitude
            val finalSample = filteredSample * amplitude
            
            // 5. Write to stereo buffer (panning if needed)
            buffer[frame * 2] = finalSample * panLeft      // Left channel
            buffer[frame * 2 + 1] = finalSample * panRight // Right channel
            
            // 6. Check if voice is done (ADSR finished release phase)
            if (adsr.isFinished()) {
                state = VoiceState.IDLE
                break
            }
        }
    }
}
```

### Step 6: ADSR State Machine

```kotlin
class ADSR(
    val attackSamples: Int,
    val decaySamples: Int,
    val sustainLevel: Float,
    val releaseSamples: Int
) {
    enum class State { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE }
    var state = State.IDLE
    private var sampleCount = 0
    private var currentLevel = 0f
    private var releaseStartLevel = 0f
    
    fun noteOn() {
        state = State.ATTACK
        sampleCount = 0
        currentLevel = 0f
    }
    
    fun noteOff() {
        releaseStartLevel = currentLevel
        state = State.RELEASE
        sampleCount = 0
    }
    
    fun getNextSample(): Float {
        currentLevel = when (state) {
            State.ATTACK -> {
                val level = sampleCount.toFloat() / attackSamples
                if (sampleCount++ >= attackSamples) { state = State.DECAY; sampleCount = 0 }
                level
            }
            State.DECAY -> {
                val level = 1f - (1f - sustainLevel) * (sampleCount.toFloat() / decaySamples)
                if (sampleCount++ >= decaySamples) { state = State.SUSTAIN; sampleCount = 0 }
                level
            }
            State.SUSTAIN -> sustainLevel
            State.RELEASE -> {
                val level = releaseStartLevel * (1f - sampleCount.toFloat() / releaseSamples)
                if (sampleCount++ >= releaseSamples) { state = State.IDLE }
                maxOf(level, 0f)
            }
            State.IDLE -> 0f
        }
        return currentLevel
    }
    
    fun isFinished() = state == State.IDLE
}
```

### Loading Samples — Asset to FloatArray

```kotlin
object SampleLoader {
    fun loadSample(context: Context, assetPath: String): FloatArray {
        // Open the WAV/OGG file from assets
        val fd = context.assets.openFd(assetPath)
        
        // Use MediaExtractor to decode any format (WAV, OGG, MP3, FLAC)
        val extractor = MediaExtractor()
        extractor.setDataSource(fd.fileDescriptor, fd.startOffset, fd.length)
        extractor.selectTrack(0)
        
        val format = extractor.getTrackFormat(0)
        val codec = MediaCodec.createDecoderByType(format.getString(MediaFormat.KEY_MIME)!!)
        codec.configure(format, null, null, 0)
        codec.start()
        
        val outputSamples = mutableListOf<Float>()
        
        // Decode all frames
        val info = MediaCodec.BufferInfo()
        var done = false
        while (!done) {
            // Feed input
            val inputIdx = codec.dequeueInputBuffer(10000)
            if (inputIdx >= 0) {
                val inputBuf = codec.getInputBuffer(inputIdx)!!
                val size = extractor.readSampleData(inputBuf, 0)
                if (size < 0) {
                    codec.queueInputBuffer(inputIdx, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                    done = true
                } else {
                    codec.queueInputBuffer(inputIdx, 0, size, extractor.sampleTime, 0)
                    extractor.advance()
                }
            }
            
            // Drain output
            val outputIdx = codec.dequeueOutputBuffer(info, 10000)
            if (outputIdx >= 0) {
                val outputBuf = codec.getOutputBuffer(outputIdx)!!
                // Convert shorts to floats
                while (outputBuf.hasRemaining()) {
                    outputSamples.add(outputBuf.short / 32768f)
                }
                codec.releaseOutputBuffer(outputIdx, false)
            }
        }
        
        codec.stop(); codec.release(); extractor.release()
        return outputSamples.toFloatArray()
    }
}
```

---

## 13. How a Game App Produces Sound — Full Journey

Game audio is fundamentally different from music app audio. Games need:
- **Hundreds of simultaneous sounds** (explosions, footsteps, music, UI)
- **3D spatial audio** (sounds positioned in 3D space around the player)
- **Interactive/adaptive music** (music changes based on game state)
- **Zero-latency triggers** (explosion must sync with visual impact — within one frame)
- **Procedural audio** (some sounds generated in real-time, not from files)

### Sound Categories in a Game

```
Game Sounds
├── SFX (Sound Effects)
│   ├── One-shot: bullet fire, footstep, pickup
│   ├── Looped: engine rumble, wind, water
│   └── 3D positional: sounds from game objects in 3D space
├── Music
│   ├── Background: streaming music (too large to load fully in RAM)
│   ├── Adaptive: changes based on game state (combat music vs. exploration)
│   └── Stingers: short musical phrases triggered by events
└── Voice
    ├── Character dialogue (lip-sync critical)
    └── Ambient voice (crowd, radio, etc.)
```

### Android APIs for Game Audio

#### SoundPool — For Short SFX

```kotlin
val soundPool = SoundPool.Builder()
    .setMaxStreams(32)           // max simultaneous sounds
    .setAudioAttributes(AudioAttributes.Builder()
        .setUsage(AudioAttributes.USAGE_GAME)
        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
        .build())
    .build()

// Load sounds at startup (from res/raw or assets)
val explosionId = soundPool.load(context, R.raw.explosion, 1)
val footstepId = soundPool.load(context, R.raw.footstep, 1)

// When game event happens:
soundPool.play(
    explosionId,
    leftVolume = 1.0f,
    rightVolume = 0.5f,    // pan right
    priority = 1,
    loop = 0,              // no loop
    rate = 1.0f            // 1.0 = normal speed, 2.0 = double speed (higher pitch)
)
```

**How SoundPool works internally:**
1. `load()`: decodes the audio file using MediaCodec → stores raw PCM in RAM
2. `play()`: creates an AudioTrack for this sound (or reuses a pool of AudioTracks)
3. Sends PCM directly to AudioFlinger via the AudioTrack

SoundPool's critical property: all sounds are **pre-decoded** at load time. No decoding at play time = minimal latency when triggered.

**SoundPool's `rate` parameter:** This IS wavetable-style pitch shifting! Setting `rate=2.0` means the PCM advances 2x faster, doubling the pitch. Setting `rate=0.5` halves the pitch. Range: 0.5 to 2.0 (one octave down to one octave up).

#### Oboe — Low Latency Game Audio

SoundPool's latency can be 100-200ms, which is too long for interactive games. **Oboe** (Google's C++ audio library) achieves <10ms latency:

```cpp
// C++ (via JNI)
#include <oboe/Oboe.h>

class GameAudioEngine : public oboe::AudioStreamDataCallback {
public:
    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream *stream,
        void *audioData,
        int32_t numFrames) override {
        
        float* outputData = static_cast<float*>(audioData);
        
        // Mix all active sounds
        for (auto& voice : activeVoices) {
            voice.render(outputData, numFrames);
        }
        
        return oboe::DataCallbackResult::Continue;
    }
    
    void init() {
        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output)
               .setFormat(oboe::AudioFormat::Float)
               .setSampleRate(48000)
               .setChannelCount(2)
               .setPerformanceMode(oboe::PerformanceMode::LowLatency)
               .setSharingMode(oboe::SharingMode::Exclusive)  // dedicated output
               .setDataCallback(this)
               .openManagedStream(mStream);
        mStream->requestStart();
    }
};
```

`PerformanceMode::LowLatency` triggers AudioFlinger to use a **fast track** (bypasses the software mixer) and enables MMAP mode on supported hardware.

`SharingMode::Exclusive` gives the app exclusive access to the audio output — other apps cannot mix in (lowers latency but reduces flexibility).

### 3D Spatial Audio

For a First-Person Shooter, enemies have positions in 3D space. When an enemy behind-left fires, you should hear the gunshot coming from behind-left.

#### Simple Panning (2D)

```kotlin
fun pan3DToStereo(azimuth: Float): Pair<Float, Float> {
    // azimuth: -PI=left, 0=center, PI=right
    val panRight = (azimuth / PI).toFloat().coerceIn(-1f, 1f)
    val leftVol = sqrt((1f - panRight) / 2f)    // equal-power panning
    val rightVol = sqrt((1f + panRight) / 2f)
    return Pair(leftVol, rightVol)
}
```

**Equal-power panning** (using sqrt) maintains constant perceived loudness as sound moves left-right. Linear panning creates a "hole in the middle" psychoacoustic effect.

#### Distance Attenuation

```kotlin
fun attenuateByDistance(distance: Float, maxDistance: Float): Float {
    // Inverse square law: power falls as 1/r²
    // For perceptual linearity: amplitude falls as 1/r
    return (1f / (1f + distance)).coerceIn(0f, 1f)
}
```

#### HRTF (Head-Related Transfer Function)

For true 3D audio with headphones, sounds above/below and front/behind require **HRTF convolution**:

- When a sound comes from your left, your right ear hears it slightly later (interaural time difference, ITD)
- And at slightly lower amplitude (interaural level difference, ILD)
- Your outer ear (pinna) shapes the frequency response depending on elevation — this is the HRTF

```
HRTF convolution:
  monoInput * hrtfLeft[azimuth][elevation] → leftEar
  monoInput * hrtfRight[azimuth][elevation] → rightEar
```

HRTF databases (like CIPIC, MIT KEMAR) contain measured impulse responses for hundreds of directions. The convolution is done with FFT:

```
Time domain: O(N²) multiplications per sample — too slow
FFT domain: O(N log N) — feasible for 128-512 sample HRTF filters
```

Android 12+ supports **Spatializer API** which handles HRTF internally:
```kotlin
// Android 12+
audioTrack.setSpatialization(true)
// Now set 3D position
soundPool.setVirtualizerEnabled(true)
```

#### Occlusion and Obstruction

Advanced game audio engines simulate:
- **Occlusion:** Sound source is behind a wall → attenuate and apply LPF (walls block high frequencies)
- **Obstruction:** Sound source and listener in same room but direct path blocked → partial attenuation
- **Reverb zones:** Different rooms have different acoustic properties (cave = long reverb, padded room = no reverb)

```
Enemy gun behind wall:
1. Raycast from enemy to player → hits wall
2. Wall occlusion factor: 0.2 (80% attenuated)
3. Wall material: concrete → strong LPF (cutoff 800 Hz)
4. Apply: enemy_sample → LPF(800 Hz) → multiply by 0.2 → output
```

### Adaptive Music — Horizontal and Vertical Re-Sequencing

Game music that changes based on game state:

#### Vertical Re-sequencing (Layering)

Multiple music layers play simultaneously. Layers fade in/out based on game state:

```
Music layers (all in sync):
Layer 1: Bass + Drums          → always plays
Layer 2: Melody (exploration)  → plays when no combat
Layer 3: Strings (combat)      → fades in when enemy nearby
Layer 4: Brass (intense combat)→ fades in when health low

State machine:
  EXPLORE → Layer 1 + 2: full
  COMBAT  → Layer 1: full, Layer 2: silent, Layer 3: full
  INTENSE → Layer 1+3: full, Layer 4: fades in
```

Each layer is a separate AudioTrack, all started at the exact same time (using `AudioTrack.setPlaybackHeadPosition` to sync):

```kotlin
// Start all layers in sync
val syncBarrier = CountDownLatch(layers.size)
layers.forEach { layer ->
    Thread {
        layer.audioTrack.play()
        syncBarrier.countDown()
        syncBarrier.await()  // All threads wait until all are ready
    }.start()
}
```

#### Horizontal Re-sequencing (Stingers)

Short musical phrases ("stingers") triggered on specific beat boundaries to transition between music sections:

```
Bar 1 playing... Bar 2 playing... Bar 3 playing...
                        ↓ event: enemy appears
                    Wait until next bar boundary
                        ↓
               Play combat stinger (4 bars)
                        ↓
               Seamlessly continue into combat music
```

This requires knowing the current music **beat position** (tempo tracking) and scheduling audio to start at exact future sample positions:

```kotlin
fun scheduleStingerAtNextBar(stingerSample: FloatArray) {
    val samplesPerBar = (beatsPerBar * 60f / bpm * sampleRate).toLong()
    val currentPos = musicTrack.playbackHeadPosition.toLong()
    val barsElapsed = currentPos / samplesPerBar
    val nextBarStart = (barsElapsed + 1) * samplesPerBar
    
    stingerTrack.setPlaybackHeadPosition(nextBarStart.toInt())
    stingerTrack.play()  // Will start exactly at next bar
}
```

### Procedural Audio in Games

Some games generate sound procedurally (from code, no samples):

#### Engine Sound

```kotlin
class EngineSound(sampleRate: Int) {
    private val oscillator = WavetableOscillator(sawtoothTable, sampleRate)
    private val filter = LowPassFilter()
    
    // Engine RPM drives synthesis
    var rpm: Float = 800f
    
    fun render(output: FloatArray) {
        // Fundamental = RPM / 60 * engineCylinders / 2
        val fundamental = rpm / 60f * 4f / 2f  // 4-cylinder 4-stroke
        oscillator.setFrequency(fundamental)
        
        // Higher RPM = brighter tone
        filter.setCutoff(500f + rpm * 0.5f, sampleRate)
        
        for (i in output.indices) {
            output[i] = filter.process(oscillator.getNextSample())
        }
    }
}
```

The engine sound changes continuously with RPM — impossible with samples without huge crossfade complexity.

#### Footstep Variation

```kotlin
class FootstepEngine {
    private val gravel = loadSample("gravel_base.wav")
    private val granular = GranularPlayer(gravel, sampleRate)
    
    fun triggerFootstep(speed: Float, groundType: String) {
        // Each footstep: random grain position, slight pitch variation
        granular.triggerGrain(
            sourcePosNormalized = Random.nextFloat(),
            grainSizeMs = 80f,
            pitchRatio = 0.95f + Random.nextFloat() * 0.1f,  // slight pitch randomization
            amplitude = 0.8f + speed * 0.2f
        )
    }
}
```

This produces natural-sounding footsteps that never repeat exactly the same — avoiding the "machine gun effect" of looping a single footstep sample.

---

## 14. MIDI in Android — From Key Press to PCM

MIDI (Musical Instrument Digital Interface) is a communication protocol — **not audio**. It transmits musical events (note on, note off, pitch bend, etc.) as small messages.

### MIDI Message Structure

```
Note On message: 3 bytes
  Byte 1: 0x9n   (status byte: 9=Note On, n=channel 0-15)
  Byte 2: 0x3C   (note number: 60=Middle C)
  Byte 3: 0x64   (velocity: 100 = moderately loud)

Note Off message: 3 bytes
  Byte 1: 0x8n   (8=Note Off, n=channel)
  Byte 2: 0x3C   (same note)
  Byte 3: 0x00   (velocity=0, ignored)

Control Change (CC): 3 bytes
  Byte 1: 0xBn   (B=Control Change)
  Byte 2: 0x07   (CC7 = Volume)
  Byte 3: 0x64   (value 100)

Pitch Bend: 3 bytes
  Byte 1: 0xEn   (E=Pitch Bend)
  Bytes 2-3: 14-bit signed value (center=0x2000, up=0x3FFF, down=0x0000)

Program Change (select instrument/patch): 2 bytes
  Byte 1: 0xCn   (C=Program Change)
  Byte 2: 0x00   (patch 0 = Acoustic Grand Piano)
```

### Android MIDI API (API 23+)

```kotlin
// Receive from a connected MIDI keyboard (USB or Bluetooth LE MIDI)
val midiManager = getSystemService(MidiManager::class.java)

midiManager.openDevice(deviceInfo, { device ->
    val port = device.openOutputPort(0)
    port.connect(object : MidiReceiver() {
        override fun onSend(data: ByteArray, offset: Int, count: Int, timestamp: Long) {
            parseMidiMessage(data, offset)
        }
    })
}, null)

fun parseMidiMessage(data: ByteArray, offset: Int) {
    val status = data[offset].toInt() and 0xFF
    val type = status and 0xF0
    val channel = status and 0x0F
    
    when (type) {
        0x90 -> {  // Note On
            val note = data[offset + 1].toInt()
            val velocity = data[offset + 2].toInt()
            if (velocity > 0) synthEngine.noteOn(note, velocity)
            else synthEngine.noteOff(note)
        }
        0x80 -> {  // Note Off
            val note = data[offset + 1].toInt()
            synthEngine.noteOff(note)
        }
        0xE0 -> {  // Pitch Bend
            val lsb = data[offset + 1].toInt()
            val msb = data[offset + 2].toInt()
            val bend = ((msb shl 7) or lsb) - 8192  // -8192 to +8191
            synthEngine.setPitchBend(bend / 8192f)   // -1.0 to +1.0
        }
        0xB0 -> {  // Control Change
            val cc = data[offset + 1].toInt()
            val value = data[offset + 2].toInt()
            when (cc) {
                1  -> synthEngine.setModWheel(value / 127f)
                7  -> synthEngine.setVolume(value / 127f)
                64 -> synthEngine.setSustainPedal(value >= 64)
                74 -> synthEngine.setFilterCutoff(value / 127f)
            }
        }
    }
}
```

### Pitch Bend Implementation

```kotlin
fun setPitchBend(bend: Float) {  // -1.0 to +1.0
    // Standard: ±2 semitones for full bend range
    val semitones = bend * 2f
    pitchBendRatio = 2f.pow(semitones / 12f)
    
    // Apply to all active voices:
    for (voice in activeVoices) {
        voice.setPitchMultiplier(voice.basePitchRatio * pitchBendRatio)
    }
}
```

### Sonivox — Android's Built-in MIDI Synthesizer

Android includes **Sonivox** (Zing/Sonivox EAS synthesizer), a small SF2-based wavetable synthesizer:

```
frameworks/base/packages/  (no...)
external/sonivox/           ← Sonivox source code
```

Sonivox is a tiny synthesizer engine that reads GM (General MIDI) SF2 soundfonts and can play all 128 GM instruments. It's what powers:
- `android.media.midi` API when no external synth is connected
- The `MidiInputPort` connected to the default device
- Some old ringtones (`.mid` files)

```kotlin
// Android MIDI API sends to Sonivox automatically if no hardware synth is connected
// Sonivox receives MIDI events → wavetable lookup → PCM → AudioTrack → speaker
```

Sonivox architecture:
```
MIDI message → EAS (Sonivox) synthesizer
                    │
                    ├── Parse MIDI event
                    ├── Look up instrument patch in SF2
                    ├── Activate voice: select sample + set parameters
                    ├── For each audio callback:
                    │       render all voices (wavetable oscillator + ADSR + filter)
                    │       mix voices
                    │       output PCM buffer
                    └── → AudioTrack → AudioFlinger → speaker
```

---

## 15. Android Audio APIs for Synthesis — Choosing the Right One

### Decision Tree

```
Need to synthesize audio?
│
├── Just play back recorded files? → MediaPlayer (simplest)
│
├── Need to trigger many short sounds quickly? → SoundPool
│   (pre-decodes everything, low allocation overhead at trigger time)
│
├── Need to generate PCM yourself (synthesizer)?
│   │
│   ├── Latency not critical (>100ms ok)?
│   │   └── AudioTrack in WRITE mode (Java/Kotlin)
│   │
│   ├── Need <20ms latency?
│   │   └── Oboe (C++, NDK) with PerformanceMode::LowLatency
│   │
│   └── Need absolute minimum latency (<10ms)?
│       └── AAudio MMAP mode (requires hardware support)
│
├── Building a music instrument app?
│   └── Oboe (handles round-trip latency measurement + optimal configuration)
│
└── Building a game?
    ├── Simple: SoundPool for SFX + MediaPlayer for music
    ├── Advanced: FMOD or Wwise (middleware, handles everything)
    └── DIY: Oboe for SFX + ExoPlayer for music
```

### AudioTrack Modes

```kotlin
// MODE_STREAM: write PCM continuously (good for synthesizers)
val track = AudioTrack(
    AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build(),
    AudioFormat.Builder()
        .setSampleRate(48000)
        .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
        .build(),
    bufferSizeInBytes,
    AudioTrack.MODE_STREAM,
    AudioManager.AUDIO_SESSION_ID_GENERATE
)

// Write loop (in a dedicated thread):
while (isPlaying) {
    val samples = synthesizer.generateNextBuffer(bufferSize)
    track.write(samples, 0, samples.size, AudioTrack.WRITE_BLOCKING)
}
```

```kotlin
// MODE_STATIC: load all PCM upfront, then play repeatedly (good for short SFX)
val track = AudioTrack(
    ...,
    totalSizeBytes,
    AudioTrack.MODE_STATIC,
    ...
)
track.write(pcmData, 0, pcmData.size)
track.play()  // Plays the static buffer
// track.reloadStaticData() to play again
```

### AAudio / Oboe Callback Model (Preferred for Low Latency)

Unlike AudioTrack's write-from-thread model, AAudio uses a **callback model** where the audio subsystem CALLS YOUR CODE at precisely timed intervals:

```kotlin
// The audio engine calls this at exactly the right time (every ~5ms)
override fun onAudioReady(stream: AudioStream, audioData: FloatArray, numFrames: Int): DataCallbackResult {
    // You have numFrames * 1/48000 seconds to generate audio
    // For 240 frames at 48kHz: 5ms to compute
    // If you take longer: underrun (dropout/click in audio)
    
    synthesizer.render(audioData, numFrames)
    return DataCallbackResult.Continue
}
```

This callback runs on a **dedicated real-time thread** managed by the audio subsystem. You must:
- Never do I/O (file reads, network)
- Never allocate memory (malloc/new)
- Never lock a mutex that might be held by another thread
- Never do anything that takes unpredictable time

This is called **real-time safe code** — it must complete in bounded, predictable time.

### Lock-Free Communication between Main Thread and Audio Thread

Since you can't lock a mutex in the audio callback, communication uses **lock-free data structures**:

```cpp
// Lock-free ring buffer for commands from main thread → audio thread
// (e.g., "trigger note 60 at velocity 90")
class LockFreeCommandQueue {
    std::atomic<int> writeIndex{0};
    std::atomic<int> readIndex{0};
    std::array<NoteCommand, 256> commands;
    
    bool push(NoteCommand cmd) {
        int next = (writeIndex.load() + 1) % 256;
        if (next == readIndex.load()) return false;  // full
        commands[writeIndex.load()] = cmd;
        writeIndex.store(next);
        return true;
    }
    
    bool pop(NoteCommand& cmd) {
        if (writeIndex.load() == readIndex.load()) return false;  // empty
        cmd = commands[readIndex.load()];
        readIndex.store((readIndex.load() + 1) % 256);
        return true;
    }
};
```

Main thread pushes commands (noteOn, noteOff, paramChange). Audio thread pops commands at the start of each callback.

---

## 16. Writing a Synthesizer in Android — Code-Level Walkthrough

Let's build a minimal but real wavetable synthesizer from scratch.

### Architecture

```
JNI Bridge
    │
    ├── SynthEngine.kt  (Kotlin wrapper)
    └── SynthEngine.cpp (C++ implementation)
        │
        ├── WavetableOscillator (per voice)
        ├── ADSR (per voice)
        ├── LowPassFilter (per voice)
        ├── VoiceManager (polyphony: 16 voices)
        ├── MasterLimiter
        └── Oboe AudioStream
```

### Android.bp / build.gradle Setup

```gradle
// app/build.gradle
android {
    defaultConfig {
        externalNativeBuild {
            cmake { cppFlags "-std=c++17" }
        }
    }
    externalNativeBuild {
        cmake { path "src/main/cpp/CMakeLists.txt" }
    }
}

dependencies {
    implementation 'com.google.oboe:oboe:1.8.0'
}
```

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.22)
project(synth)

find_package(oboe REQUIRED CONFIG)

add_library(synth SHARED
    SynthEngine.cpp
    WavetableOscillator.cpp
    ADSR.cpp
    VoiceManager.cpp
)

target_link_libraries(synth oboe::oboe log android)
```

### Wavetable Generation (Pre-computed at startup)

```cpp
// WavetableFactory.cpp
#include <cmath>
#include <vector>

constexpr int kWavetableSize = 4096;

std::vector<float> generateSineWavetable() {
    std::vector<float> table(kWavetableSize);
    for (int i = 0; i < kWavetableSize; ++i) {
        table[i] = sinf(2.0f * M_PI * i / kWavetableSize);
    }
    return table;
}

// Band-limited sawtooth: sum of harmonics up to Nyquist
std::vector<float> generateBandLimitedSawtooth(float fundamentalFreq, int sampleRate) {
    std::vector<float> table(kWavetableSize, 0.0f);
    int maxHarmonic = (int)(sampleRate / 2.0f / fundamentalFreq);
    
    for (int h = 1; h <= maxHarmonic; ++h) {
        float amplitude = 1.0f / h;  // harmonic series
        float sign = (h % 2 == 0) ? -1.0f : 1.0f;
        for (int i = 0; i < kWavetableSize; ++i) {
            table[i] += sign * amplitude * sinf(2.0f * M_PI * h * i / kWavetableSize);
        }
    }
    
    // Normalize
    float maxVal = *std::max_element(table.begin(), table.end());
    if (maxVal > 0) for (auto& s : table) s /= maxVal;
    
    return table;
}
```

### The Complete Audio Callback

```cpp
// SynthEngine.cpp
oboe::DataCallbackResult SynthEngine::onAudioReady(
    oboe::AudioStream* stream,
    void* audioData,
    int32_t numFrames) {
    
    float* output = static_cast<float*>(audioData);
    
    // Clear output buffer
    memset(output, 0, numFrames * 2 * sizeof(float));  // stereo: 2 channels
    
    // Process incoming MIDI commands (lock-free queue)
    NoteCommand cmd;
    while (commandQueue.pop(cmd)) {
        if (cmd.isNoteOn) voiceManager.noteOn(cmd.midiNote, cmd.velocity);
        else voiceManager.noteOff(cmd.midiNote);
    }
    
    // Render all voices
    for (int v = 0; v < kMaxVoices; ++v) {
        Voice& voice = voiceManager.voices[v];
        if (!voice.isActive()) continue;
        
        for (int frame = 0; frame < numFrames; ++frame) {
            float osc = voice.oscillator.getNextSample();
            float filtered = voice.filter.process(osc);
            float enveloped = filtered * voice.adsr.getNextSample();
            
            // Simple equal-power stereo spread
            float pan = voice.pan;  // -1 left, 0 center, 1 right
            output[frame * 2]     += enveloped * sqrtf(0.5f * (1.0f - pan));  // L
            output[frame * 2 + 1] += enveloped * sqrtf(0.5f * (1.0f + pan));  // R
        }
    }
    
    // Master limiter (soft clip)
    for (int i = 0; i < numFrames * 2; ++i) {
        // Tanh soft clipper: never clips, smoothly saturates
        output[i] = tanhf(output[i]);
    }
    
    return oboe::DataCallbackResult::Continue;
}
```

---

## 17. Game Audio Engines — FMOD, Wwise, OpenAL

Most serious games don't build their own audio engine. They use middleware.

### FMOD Studio

FMOD is the most popular game audio middleware on Android:

```
Games using FMOD: Fortnite, Minecraft, Cuphead, Hollow Knight, Celeste
```

**FMOD Architecture:**
```
FMOD Studio (designer tool) → .bank files (compiled audio data + event definitions)
    │
    ▼
FMOD Engine (runtime, C++ library) → links into your Android app
    │
    ├── Event system: "Play event 'explosion/small'" → FMOD selects sample, position, volume
    ├── DSP graph: effects chain (reverb, EQ, compression)
    ├── 3D spatializer: HRTF, distance, occlusion
    ├── Mix groups: separate buses for SFX, music, voice
    └── → Oboe/AAudio output → AudioFlinger → speaker
```

```kotlin
// FMOD in Android (Kotlin JNI wrapper)
val system = FMOD.System_Create()
system.init(512, FMOD_INIT_3D_RIGHTHANDED, null)

// Load bank files (compiled in FMOD Studio)
system.loadBankFile("master.bank", FMOD_STUDIO_LOAD_BANK_NORMAL)
system.loadBankFile("master.strings.bank", FMOD_STUDIO_LOAD_BANK_NORMAL)

// Get event description
val explosionDesc = system.getEvent("event:/SFX/explosion_large")
val explosion = explosionDesc.createInstance()

// Set 3D position
val pos = FMOD_3D_ATTRIBUTES()
pos.position.x = 10f; pos.position.y = 0f; pos.position.z = 5f
explosion.set3DAttributes(pos)

explosion.start()

// In game loop: update listener position
val listenerAttr = FMOD_3D_ATTRIBUTES()
listenerAttr.position.set(player.x, player.y, player.z)
system.setListenerAttributes(0, listenerAttr)

system.update()  // Must call every frame
```

### Wwise

Used in AAA games: God of War, FIFA, Call of Duty.

Wwise adds **interactive music** features beyond FMOD:
- Adaptive music system with states, switches, RTPC (Real-Time Parameter Controls)
- Hierarchical mix system
- Profiler and authoring tool integration during development

### OpenAL — The OpenGL of Audio

OpenAL (Open Audio Library) is a cross-platform C API for 3D audio, inspired by OpenGL:

```c
// OpenAL in Android (via libopenal, bundled with app)
ALCdevice* device = alcOpenDevice(nullptr);    // default audio device
ALCcontext* context = alcCreateContext(device, nullptr);
alcMakeContextCurrent(context);

// Generate buffer + source
ALuint buffer, source;
alGenBuffers(1, &buffer);
alGenSources(1, &source);

// Load PCM data into buffer
alBufferData(buffer, AL_FORMAT_STEREO16, pcmData, dataSize, 44100);

// Attach buffer to source, set 3D properties
alSourcei(source, AL_BUFFER, buffer);
alSource3f(source, AL_POSITION, 10.0f, 0.0f, 5.0f);  // 3D position
alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
alSourcef(source, AL_GAIN, 1.0f);

// Set listener position
alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
alListenerfv(AL_ORIENTATION, orientation);  // forward + up vectors

alSourcePlay(source);  // trigger!
```

OpenAL's math (distance model, Doppler, HRTF) is computed in the app process and outputs stereo PCM via OpenSL ES (old) or Oboe (wrapped), which then goes to AudioFlinger.

---

## 18. Effects DSP — Reverb, Chorus, Delay

Effects transform the dry (unprocessed) PCM signal into something richer.

### Delay — Echo

The simplest effect: store past output in a circular buffer, mix with current output:

```kotlin
class DelayEffect(
    delayMs: Float,
    sampleRate: Int,
    val feedback: Float = 0.4f,
    val wetMix: Float = 0.3f
) {
    private val delayBuffer = FloatArray((delayMs / 1000f * sampleRate).toInt())
    private var writeIndex = 0
    
    fun process(input: Float): Float {
        val readIndex = (writeIndex - delayBuffer.size + delayBuffer.size) % delayBuffer.size
        val delayed = delayBuffer[readIndex]
        
        // Store current input + feedback of delayed signal
        delayBuffer[writeIndex] = input + delayed * feedback
        writeIndex = (writeIndex + 1) % delayBuffer.size
        
        // Mix dry + wet
        return input * (1f - wetMix) + delayed * wetMix
    }
}
```

### Reverb — Simulating Room Acoustics

Reverb simulates the complex reflections of sound in a room. Real reverb consists of:
- **Pre-delay:** Brief silence before reflections (larger room = longer pre-delay)
- **Early reflections:** Distinct echoes from walls (give "size" impression)
- **Late reverberation:** Dense, diffuse tail that decays exponentially

```
Direct sound
    │
    ├──────────────────── [pre-delay] ─────→ [early reflections 1ms-80ms]
    │                                               │
    │                                               ├── wall 1 (left): 12ms
    │                                               ├── wall 2 (right): 15ms
    │                                               ├── ceiling: 22ms
    │                                               └── floor: 8ms
    │                                               │
    │                                               ▼
    │                                        [late reverb tail: 0.5-5 seconds]
    │                                        (exponentially decaying noise)
    ▼
Output = Direct + Early Reflections + Late Reverb
```

**Algorithmic Reverb (Schroeder/Moorer):** Uses multiple delay lines + all-pass filters:

```kotlin
class SchroederReverb(sampleRate: Int) {
    // 4 parallel comb filters (delayed feedback loops)
    private val combDelays = intArrayOf(1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116)
    private val combBuffers = combDelays.map { FloatArray(it) }
    private val combIndices = IntArray(combDelays.size) { 0 }
    
    // 2 series all-pass filters (for diffusion)
    private val allpassDelays = intArrayOf(225, 556)
    private val allpassBuffers = allpassDelays.map { FloatArray(it) }
    
    val roomSize = 0.9f   // 0-1: small room to large hall
    val damping = 0.5f    // High frequencies decay faster
    
    fun process(input: Float): Float {
        var output = 0f
        
        // Sum all comb filter outputs
        for (i in combBuffers.indices) {
            val buf = combBuffers[i]
            val idx = combIndices[i]
            val delayed = buf[idx]
            
            buf[idx] = input + delayed * roomSize * (1f - damping)
            combIndices[i] = (idx + 1) % buf.size
            output += delayed
        }
        output *= 0.015f  // normalize gain
        
        // Run through all-pass filters for diffusion
        for (i in allpassBuffers.indices) {
            val buf = allpassBuffers[i]
            val delayed = buf[0]
            val newSample = output + delayed * 0.5f
            System.arraycopy(buf, 0, buf, 1, buf.size - 1)
            buf[0] = newSample
            output = delayed - 0.5f * newSample
        }
        
        return output
    }
}
```

**Convolution Reverb:** Instead of algorithmic simulation, capture the actual impulse response of a real space (fire a starter pistol in Notre-Dame cathedral, record it). Convolve any audio with this impulse response → sounds like it's in Notre-Dame.

```
IR of Carnegie Hall (96000 sample stereo impulse response)
Convolve with your guitar signal
= Guitar sounds like it's playing live in Carnegie Hall

// FFT-based convolution (overlap-add method):
Signal blocks → FFT → multiply with FFT(IR) → IFFT → output
Complexity: O(N log N) per block instead of O(N²) direct convolution
```

### Chorus

Chorus creates the illusion of multiple instruments by pitch-modulating slight delays:

```kotlin
class ChorusEffect(sampleRate: Int) {
    private val maxDelaySamples = (30f / 1000f * sampleRate).toInt()
    private val delayBuffer = FloatArray(maxDelaySamples)
    private var writeIndex = 0
    
    // LFO modulates delay time (creates slight pitch wobble)
    private var lfoPhase = 0.0
    private val lfoRate = 0.5f   // Hz (0.1-5 Hz range)
    private val depth = 0.3f     // modulation depth
    
    fun process(input: Float): Float {
        // Store in delay buffer
        delayBuffer[writeIndex] = input
        
        // LFO: sinusoidal delay modulation
        val lfoValue = sin(2.0 * PI * lfoPhase).toFloat()
        lfoPhase += lfoRate / sampleRate
        if (lfoPhase >= 1.0) lfoPhase -= 1.0
        
        // Compute modulated delay (in samples)
        val delaySamples = maxDelaySamples * 0.5f * (1f + depth * lfoValue)
        val readIndex = ((writeIndex - delaySamples.toInt() + maxDelaySamples) % maxDelaySamples)
        
        writeIndex = (writeIndex + 1) % maxDelaySamples
        
        return input * 0.5f + delayBuffer[readIndex] * 0.5f  // 50% dry + 50% wet
    }
}
```

---

## 19. Latency — Why It Matters for Instruments

For a music instrument app, latency is the difference between good and unusable:

```
Latency budget for a piano app:
  Touch input detection:    ~16ms (1 frame at 60fps)
  Android input dispatch:    ~5ms
  JNI call to audio engine:  ~1ms
  Audio buffer fill delay:  varies (the biggest factor)
  AudioFlinger processing:   ~2ms
  HAL + DMA:                 ~2ms
  DAC + speaker:             ~2ms
  
  Total: ~28ms + audio buffer time
```

The **audio buffer size** dominates:
```
Buffer size 4096 frames at 48kHz = 85ms of latency (TERRIBLE for instruments)
Buffer size 1024 frames at 48kHz = 21ms (barely acceptable)
Buffer size 256 frames at 48kHz  = 5.3ms (excellent)
Buffer size 128 frames at 48kHz  = 2.7ms (best achievable)
```

Musicians consider <10ms "low latency" (like acoustic instruments), 10-20ms "acceptable", >40ms "unusable for real-time playing".

### MMAP Mode — Ultra Low Latency

**MMAP (Memory-Mapped) mode** in AAudio: the app's audio buffer IS the DMA buffer. No copy needed:

```
Normal AudioTrack flow:
  App buffer → copy → AudioFlinger buffer → copy → HAL buffer → DMA → hardware

MMAP flow:
  App buffer = HAL buffer = DMA buffer → hardware
              (zero copies!)
```

This eliminates one or two audio buffer delays, achieving ~2ms latency on supported hardware.

```kotlin
// Oboe will automatically use MMAP if available
builder.setPerformanceMode(PerformanceMode.LowLatency)
// On supported hardware (Pixel 3+, many newer phones), Oboe uses MMAP automatically
```

### Latency Measurement

Oboe provides a round-trip latency measurement:
1. App generates a click sound
2. App records mic input
3. Measures time from click output to echo detected in mic input
4. Round-trip latency = half of measured delay

```kotlin
// Oboe latency tuner
val latencyTuner = LatencyTuner(audioStream)
// In callback:
latencyTuner.tune()  // adjusts buffer size dynamically for minimum latency
```

---

## 20. Appendix — Mathematics of Audio DSP

### Decibels (dB)

Audio levels are almost always expressed in decibels because human hearing is logarithmic.

```
Power ratio in dB:   dB = 10 * log10(P2/P1)
Amplitude ratio in dB: dB = 20 * log10(A2/A1)  (since Power ∝ Amplitude²)

Common values:
  +6 dB  = 2× amplitude  (double the loudness in linear terms)
  +20 dB = 10× amplitude
  0 dB   = reference level (no change)
  -6 dB  = 0.5× amplitude  (half volume)
  -20 dB = 0.1× amplitude
  -60 dB = 0.001× amplitude  (nearly silent)
  -∞ dB  = 0× amplitude  (complete silence)
  
Volume control: 
  Android volume step (0-15) to amplitude multiplier:
  amplitude = pow(10, (volumeStep - 15) * 0.1f)
  // Step 15 = 0dB = full amplitude = 1.0
  // Step 0  = -15dB = ~0.177 amplitude (not silent! that's step -∞)
```

### Nyquist-Shannon Sampling Theorem

**Theorem:** To perfectly reconstruct a signal, the sample rate must be at least twice the highest frequency in the signal.

```
Highest human-audible frequency: ~20,000 Hz
Required sample rate: >= 40,000 Hz
CD standard: 44,100 Hz (provides headroom above 20,000 Hz)
Broadcast/professional: 48,000 Hz (most Android processing)

Nyquist frequency = sample_rate / 2
At 48kHz: Nyquist = 24,000 Hz
Any frequency above 24,000 Hz cannot be represented → aliases back into audible range
```

### Convolution — The Math of Filters and Reverb

**Convolution** of two signals x[n] and h[n]:

```
(x * h)[n] = Σ x[k] * h[n-k]   for k = 0 to N-1
```

For a simple 3-point moving average filter:
```
h = [1/3, 1/3, 1/3]

y[n] = x[n]*1/3 + x[n-1]*1/3 + x[n-2]*1/3

This is convolution with a 3-sample boxcar window = a simple low-pass filter
```

For reverb, h is the Room Impulse Response (RIR) — typically 48000 to 480000 samples long.

### The FFT — Fast Fourier Transform

The FFT converts a time-domain signal (samples over time) to frequency-domain (amplitude at each frequency):

```
Time domain: [0.1, 0.5, 0.3, -0.2, -0.4, ...]   (48000 numbers per second)
FFT ↓
Frequency domain: [0Hz: 0.0, 100Hz: 0.2, 440Hz: 0.8, 880Hz: 0.4, ...]

Why useful in synthesis?
  - Spectral analysis: see what frequencies a sound contains
  - Convolution: multiply in frequency domain = convolve in time domain (faster)
  - Resynthesis: modify frequency components, IFFT back to audio
  - Pitch detection: find dominant frequency (for autotune, tuner apps)
  - Audio visualization (equalizer bars): each bar = amplitude at a frequency range

FFT size (N) determines frequency resolution:
  N=1024, SR=48000 → frequency resolution = 48000/1024 = 46.875 Hz per bin
  N=4096, SR=48000 → frequency resolution = 48000/4096 = 11.72 Hz per bin (better resolution, more latency)
```

### IIR Biquad Filter — The Universal Filter Building Block

Almost all digital filters in synthesizers are **IIR (Infinite Impulse Response) Biquad** filters. They're cheap (5 multiplications + 4 additions per sample) and can implement any filter type:

```
Biquad difference equation:
  y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]
        - a1*y[n-1] - a2*y[n-2]

  where x = input, y = output
  b0, b1, b2, a1, a2 = coefficients (computed from desired cutoff + Q)

LPF coefficients (from RBJ Audio EQ Cookbook):
  w0 = 2π * cutoff / sampleRate
  alpha = sin(w0) / (2*Q)
  b0 = (1 - cos(w0)) / 2
  b1 = 1 - cos(w0)
  b2 = (1 - cos(w0)) / 2
  a0 = 1 + alpha
  a1 = -2 * cos(w0)
  a2 = 1 - alpha
  (all coefficients divided by a0 for normalization)
```

```kotlin
class BiquadFilter {
    private var b0=1f; private var b1=0f; private var b2=0f
    private var a1=0f; private var a2=0f
    private var x1=0f; private var x2=0f  // input history
    private var y1=0f; private var y2=0f  // output history
    
    fun setLowPass(cutoffHz: Float, Q: Float, sampleRate: Int) {
        val w0 = 2f * PI.toFloat() * cutoffHz / sampleRate
        val alpha = sin(w0) / (2f * Q)
        val cosW0 = cos(w0)
        val a0inv = 1f / (1f + alpha)
        b0 = (1f - cosW0) / 2f * a0inv
        b1 = (1f - cosW0) * a0inv
        b2 = b0
        a1 = (-2f * cosW0) * a0inv
        a2 = (1f - alpha) * a0inv
    }
    
    fun process(x: Float): Float {
        val y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2
        x2=x1; x1=x; y2=y1; y1=y
        return y
    }
}
```

---

## Summary — The Synthesis Map

```
HOW DOES A PIANO APP PRODUCE SOUND?

User touches key (MIDI note 60, velocity 90)
    │
    ▼
Voice allocation: find nearest sampled note (C4)
Find matching velocity layer (velocity 90 → "forte" samples)
    │
    ▼
SamplePlayer: position = 0, pitch ratio = 1.0 (exact C4)
ADSR: state = ATTACK
Filter: cutoff from velocity (louder = brighter)
    │
    ▼ (in audio callback, every 5ms)
SamplePlayer reads pre-decoded PCM with linear interpolation
    × ADSR envelope (attack → decay → sustain)
    → BiquadFilter LPF
    × amplitude (velocity mapping)
    │
    ▼
Accumulated with all other active voices (polyphony)
    │
    ▼
Master limiter (tanh soft clip)
    │
    ▼
Oboe callback returns float buffer to AudioFlinger
    │
    ▼
AudioFlinger → HAL → DAC → Speaker

HOW DOES A GAME SFX WORK?

Explosion event triggers
    │
    ▼
SoundPool.play(explosionId, leftVol, rightVol, priority, 0, rate)
    │
    ▼ (pre-decoded PCM already in RAM)
AudioFlinger creates a new track, starts playback from PCM buffer
    │
    ▼
3D position applied: pan + distance attenuation already baked into leftVol/rightVol
    │
    ▼
AudioFlinger mixes with music track + other SFX
    │
    ▼
HAL → DAC → Speaker

THE KEY INSIGHT:

Every synthesis method — wavetable, FM, sample playback, physical modeling —
is just a different way to fill a FloatArray with PCM samples.

Once that array is filled, it's handed to AudioFlinger.
AudioFlinger doesn't care HOW the samples were generated.
The hardware doesn't care either.

Synthesis is the art and science of filling that array with
the right numbers at the right time.
```

---

*Journal written for: Android developer (5+ yrs) learning AOSP audio internals + DSP fundamentals.*
*Companion to: `audioJournal.md`*
*AOSP reference: android-15.0.0_r14 / Android 15*
*Last updated: 2026-04*
