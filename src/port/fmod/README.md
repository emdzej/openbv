# FMOD shim

BaboViolent 2 plays sound through FMOD 3 (`FSOUND_*`, via `src/engine/dk/dks.cpp`, and a few
direct calls in the game). This directory implements that API on a software mixer, so `dks.cpp`
and the game build unchanged.

| File | What |
|---|---|
| `include/fmod.h` | FMOD 3.75's names, signatures and constants (only what we implement is declared) |
| `include/fmod_errors.h` | `FMOD_ErrorString` |
| `include/fmodshim.h` | Platform side: `fmodshim_set_reader`, `fmodshim_mix_rate`, `fmodshim_render` |
| `fmodshim.c` | Channels, samples, streams, 3D, the mixer |
| `fmodshim_stdio.c` | stdio reader for native tests and tools (root dir, case-insensitive fallback) |
| `vorbis.c`, `third_party/stb_vorbis.c` | Ogg Vorbis: stb_vorbis v1.22 (nothings/stb `2c980bb`, public domain / MIT) |

## Platform contract

- `fmodshim_set_reader(read)`: `read(name, &size)` returns a `malloc`'d buffer (the shim frees it)
  or NULL. Names are what the game passes (`main/sounds/Button.wav`). The shim never opens files.
- `FSOUND_Init(mixrate, ...)` sets the mix rate (BV2's `s_mixRate`, default 22050). The platform
  configures its output with `fmodshim_mix_rate()` and 2 channels, and calls
  `fmodshim_render(float *out, int frames)` each frame: interleaved stereo, clipped to [-1, 1].
- Single-threaded, no clock: output depends only on the call sequence.

## Behaviour

- **Channels:** `maxsoftwarechannels` of them (BV2: 16), shared by samples and streams. A handle is
  `index | serial << 12`, so a handle kept from an earlier sound fails once the channel is reused (a
  bare index also works). `FSOUND_FREE` takes the lowest free channel, else steals the oldest of the
  lowest priority not above the new sound's (samples 128, streams 256: music is never stolen).
  `FSOUND_ALL` works for stop, volume, pan, pause, frequency and loop mode.
- **Samples:** PCM WAV, 8 or 16 bit, mono or stereo, any rate (that is all the data has). Loop
  modes: off and normal over the whole sample (bidi plays as normal). A new play takes the sample's
  defaults: volume 255, pan 128, its own rate, 3D at the origin, min 1, max 1e9.
- **`FSOUND_Sample_SetMode`:** loop bits replace the loop mode, `FSOUND_2D` takes the sample out of 3D
  for good. `dksPlaySound` sets `FSOUND_2D` before each play and `dksPlay3DSound` never clears it, so
  a sample once played 2D plays 2D from then on (FMOD 3 behaved this way, as far as its documentation
  goes).
- **Mixing:** linear interpolation, 32.32 fixed-point position, step `freq / mixrate`. The pan law is
  linear: left `(255 - pan) / 255`, right `pan / 255` (centre is about half on each side), times
  `volume / 255` and the SFX master volume `/ 255`, which applies to streams too. `FSOUND_STEREOPAN`
  gives full volume on both sides.
- **3D:** recomputed at mix time from the listener as of the last `FSOUND_Update` (BV2 sets the
  listener, then calls `FSOUND_Update`, once a frame). Gain is 1 within min distance, otherwise
  `min / (min + rolloff * (d - min))` with `d` clamped to max (rolloff 1, so `min / d`). Pan is
  `128 + 127.5 * dot(direction to the sound, listener right)`, where right = top x forward (FMOD 3 is
  left-handed). BV2's listener looks down +z with +y on top, so world +x is right. The distance and
  doppler factors are stored but only affect doppler, and BV2 passes no velocities.
- **Streams:** Ogg Vorbis (decoded as it plays from the file kept in memory: `Music.ogg` is 4.9 MB,
  and decoding it whole would take about 54 MB) or WAV. Streams are 2D. MP3 is not supported:
  `IntroScreen.mp3`, the only one referenced, is not in the data either.

## Uncertain (from FMOD 3's documentation, not checked against the DLL)

- The pan law (linear vs. constant power) and whether centre pan halves a stereo sample.
- Channel stealing when every channel is busy at equal priority (here: the oldest is stolen).
- Whether streams are 3D by default (here: 2D; the in-game music would be nearly silent otherwise).
- Whether a 3D change applies at once or only after `FSOUND_Update` (here: channel attributes at
  once, the listener at the update).
- Constant values above `FSOUND_VAG` (`FSOUND_NONBLOCKING` and up); BV2 uses none of them.

The test is `tests/audio/` (see its README).
