# FMOD shim test

Native test of `src/port/fmod` (the FSOUND subset BV2 uses) against the real sounds.

```sh
mkdir -p build out/audio
cc -std=c99 -O2 -Wall -Isrc/port/fmod/include -Isrc/port/fmod \
  tests/audio/audio_test.c src/port/fmod/fmodshim.c src/port/fmod/fmodshim_stdio.c src/port/fmod/vorbis.c \
  -lm -o build/audio_test
./build/audio_test ref/BaboViolent2/BaboViolent2/Content out/audio/test.wav
```

Run from the repo root. Prints `ok`/`FAIL` per check, peak/RMS per segment, the output hash, and
`PASS`; writes `out/audio/test.wav` (22050 Hz stereo) to listen to.
