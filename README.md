# le bac

badge audio composer: qguv's music tracker for the rvasec 2018 conference badge created by hackrva

<a href="https://ptpb.pw/AD9nwXbGVuOGHGk0mJkGuLfvmBNN.gif"><img src="https://ptpb.pw/AD9nwXbGVuOGHGk0mJkGuLfvmBNN.gif"></a>

## About

The 2017 badge had a speaker connected to a pin of the pic32 MCU. By setting the pin high, we could push the speaker to its limit in one direction; setting the pin low would let the speaker relax and eventually return to its neutral position. This was fairly quiet, but good enough for simple, constant-frequency beeps.

For the 2018 badge, I begged for some way to drive the speaker in more than a single direction. Paul eventually caved and added in an H-bridge, a component that's usually used for driving motors, and connected it to _two_ MCU pins. This lets us push the speaker in one direction (by setting the first pin high and the second low); pull the speaker (by setting the first pin low and the second high); quickly neutralize the speaker back to its natural resting position (by setting both pins high); or let the speaker return to its resting position on its own (by setting both pins low).

The addition of the H-bridge turns one-bit audio (speaker is either neutral or pushed forward) into "one-and-a-half-bit" audio (speaker is either pulled backward, neutral, or pushed forward). This has several nice consequences:

  - potential for higher-resolution audio
  - much louder sound output if driven at a high duty cycle
  - potential for more interesting waveforms: variable shape, duty cycle, etc.

The ability to create more than just square waves opens the door to something that really **doesn't belong on a speaker like this one: two channel audio.** The holy grail for this project was to have two sounds being synthesized in software and mixed in such a way that both sounds are clearly audible in a final signal that's free of distortions and dissonant harmonics.

To actually accomplish this, we had to overcome several challenges:

  - an extremely CPU-limited audio interrupt handler (on the order of 50 cycles per interrupt) to avoid timing out or hogging the CPU from running badge apps
  - virtually no memory
  - a proprietary compiler with a personal vendetta against me

The CPU limitations of the interrupt handler can be overcome by doing much of the necessary computation before any code is transferred to the badge. We found that small wavetables can greatly reduce the number of ALU operations (especially divisions) needed to synthesize and mix audio. This is the primary purpose of `lebac`: to do as much computation as possible up front and compile the results into tables and lists of sound instructions that the interrupt handler and other badge apps can understand.

Making a TUI tracker was fun, but two real signal processing challenges had to be addressed:

  1. what wave shapes can best be combined to minimize distortion when mixed together and crunched into one-and-a-half-bit space?
  2. what is the best way to crunch an arbitrary waveform (the sum of the two oscillators) into one-and-a-half-bit space?

For me, the answers were _low-duty asymmetrical neutralizing pulse waves_ and _summing and setting threshholds_. You can read the `audio(...)` function in src/main.c for a detailed description of precisely how these are done, but for now, I'll give a general overview.

Now that there's the option to neutralize the speaker (pull it to its resting state), the number of waves that can be made is much greater. Where the 2017 badge could produce square waves of various pulse widths, the 2018 badge can use its H-bridge to approximate several interesting wave shapes:

  - sin/triangle waves: pull, neutral, push, neutral; pull, neutral, push, neutral; ...
  - saw/invsaw waves: pull, neutral push; pull, neutral, push; ...
  - full-cycle square/pulse waves: pull, push; pull, push; ...
  - half-cycle square/pulse waves: pull, neutral; pull, neutral; ... or push, neutral; push neutral; ...
  - noise

All the shapes listed above are symmetrical, but we're not limited to symmetrical shapes. We can also have:

  - asymmetrical neutralizing pulse: pull, neutral, neutral, neutral, push, neutral; ...
  - asymmetrical saw waves: pull, pull, pull, neutral, neutral, push; ...
  - arbitrarily complex waves: e.g. pull, neutral, pull, neutral, neutral, push, push, neutral; ...

As it turns out, using the first of these with a very low duty cycle produces waves that are non-neutral infrequently enough not to interfere with each other too often while also producing enough variance when summing oscillators that any chaotic section of the combined wave doesn't repeat frequently enough to be perceptibly dissonant. This means our waves look like:

```
__,                           ,__,                           ,__,
  |___________________,  ,____|  |___________________,  ,____|  |______....
                      |__|                           |__|
                                   ,
                                  -+-
                                   '
_,              ,_,              ,_,              ,_,              ,_,
 |_________, ,__| |_________, ,__| |_________, ,__| |_________, ,__| |_...
           |_|              |_|              |_|              |_|

                                 ____
                                 ____

__,             ,_,           ,____,              ,_,        ,_,   ,_,
  |________, ,__| |___,  ,__, |    |_________, ,__| |,  ,____| |___| |_....
           |_|        |__|  |_|              |_|     |__|

```

By transferring simple wavetables onto the badge, the computation needed to generate two such waveforms and sum them is minimal.

## Building

`out123` (from mpg123) or `aplay` (from alsa-utils) is required for audio output.

The termbox TUI library is statically linked into the generated executable.

Once you have one of those, type `make`.

## Usage

To run:

```
./build/lebac [filename]
```

Press '?' for a list of keybindings. Press Ctrl-C twice to exit.

## Thanks

- [**Stephen M Cameron**](https://github.com/smcameron) for [many features and build improvements](https://github.com/qguv/lebac/commits?author=smcameron)
- [**Carter Hall**](https://github.com/cahruhr) for signal processing theory and help working around embedded system constraints
- [**Jonathan Lundquist**](https://github.com/jonathan46000) for project management and patience
- **Paul** for embedded systems expertise and an example implementation of an audio asset management library
