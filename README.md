# JX Step Sequencer (VST3)

A VST3 port of the Max for Live device **JX Step Sequencer**: a Roland JX-3P style
polyphonic step sequencer with REC, Rest, Tie, PLAY, Clear, step select (< >),
Erase and a Rate switch (1/4, 1/8, 1/16, 1/32). Up to 128 steps, 6 notes per step.

It works like the M4L device:

- **REC** starts a new sequence at step 1. Each chord you play becomes one step
  (it is stored when you let go of all keys).
- **Rest** / **Tie** add a silent step / hold the previous step through one more.
- **PLAY** arms playback. Steps run on the host's tempo grid while the host
  transport is playing; starting or stopping the transport goes back to step 1.
- **< >** select a step, **Erase** blanks it. Play a chord to fill a blank step.
  While recording, Erase removes the last step you entered.
- The sequence and Rate are saved with your project. The keyboard input is
  passed through to the output as well.
- The window can be resized from the bottom-right corner.
- **HELP** turns the hover help texts on or off.
- **MIDI**: drag it onto Live's Arrangement (or a clip slot, or any DAW track) to drop
  one pass of the sequence in as a MIDI clip, at the current Rate. Ties become longer
  notes, rests become gaps. Click it instead to save a `.mid` file to
  `~/Music/JX Step Sequencer/` and show it in Finder.

The sequencer logic is in `Source/SequencerEngine.h`, a direct port of `jxseq.js`.
The screen is a port of `jxscreen.js`.

## Build on your Mac (macOS 12 or later)

1. Install Apple's command line tools: open Terminal and run `xcode-select --install`
   (Xcode 14 also works).
2. Install CMake: `brew install cmake`, or download it from cmake.org.
3. In Terminal, go to this folder and run:

   ```
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```

   The first run downloads JUCE (about 5 minutes). The plugin is copied to
   `~/Library/Audio/Plug-Ins/VST3/JX Step Sequencer.vst3` automatically.

## Or let GitHub build it

Push this folder to a GitHub repository. The workflow in `.github/workflows`
builds a universal (Apple Silicon + Intel) VST3. Download the zip from the
run's **Artifacts** section, unzip it into `~/Library/Audio/Plug-Ins/VST3/`, then run:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"JX Step Sequencer.vst3"
```

## Using it in Ableton Live

Live has no MIDI-effect slot for plugins, so the sequencer loads as an **instrument**
that sends MIDI to another track:

1. Preferences → Plug-Ins: turn on **Use VST3 Plug-In System Folders**, then **Rescan**.
2. **Track 1:** drop *JX Step Sequencer* on a MIDI track and arm it, so your keyboard plays into it.
3. **Track 2:** put your synth on another MIDI track. Set **MIDI From** to Track 1 and
   choose *JX Step Sequencer* in the lower menu. Set **Monitor** to **In**.
4. Press REC on the plugin, play chords, press REC again, then PLAY and start Live's transport.

In Bitwig, Reaper and other VST3 hosts, put it before the synth on the same track
(or route its MIDI output to the synth).

## Tests

`tests/` contains a small test program for the engine, timing and state saving
(not needed to build the plugin): `cmake -S tests -B build-tests -DJUCE_DIR=<path to JUCE>`.
