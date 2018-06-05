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

each rendered `.wav` corresponds with a matching `.bac` midi-like file. we use an imbalanced neutralizing pulse wave algorithm (push for an entry, sit for ten entries, pull for an entry, sit for four entries). for more information on how the audio is generated, see README.md in the top-level of this repository.
