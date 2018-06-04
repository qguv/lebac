## bac binary format

badge audio composition files are formatted in the following way:

  - a magic number value of `0xbadge18`
  - the zero byte
  - one (unsigned) byte representing the tempo; cannot be less than `0x10`
  - `0x01` if emulating badge audio by default when loading this track; otherwise `0x00`
  - for each page of 16 notes:
      - for each step:
          - if the step continues playing the last triggered note, `0x00`
          - if the step kills the currently triggered note, one `0xff` byte
          - a byte from 1 to 63 representing a note to play, starting from C2
          - a byte for the duty cycle, currently ignored :(

## wav files

each rendered `.wav` corresponds with a matching `.bac` midi-like file. files ending `_good.wav` use an imbalanced neutralizing pulse wave algorithm (push for 28 cycles, sit for 356, pull for 28, sit for 100). files ending `_bad.wav` use a balanced 25% neutralizing pulse wave pattern (push for 128 cycles, sit for 128, pull for 128, sit for 128). finally, files ending with `_realbad.wav` use a sine wavetable with the two half-amplitude results added together and crunched with a threshhold.

for more information on how the audio is generated, see README.md in the top-level of this repository.

## why are there headers in here

those are the wavetables I've been experimenting with. `balanced_tri.h` corresponds with the `_bad.wav` files and ` imbalanced_tri.h` corresponds to the `_good.wav` files. replace `build/tables/sintable.h` with the contents of either one to experiment with non-sin waveforms.
