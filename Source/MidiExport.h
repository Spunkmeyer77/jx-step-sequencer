// MidiExport.h  --  turns the step sequence into a standard MIDI file
//
// One pass of the sequence, laid out exactly as it plays back:
//   * a note step starts its notes at the step boundary,
//   * each following Tie step extends them by one step,
//   * the notes stop at the next Rest or note step (full-length gate).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SequencerEngine.h"

#include <vector>

namespace jx
{

constexpr int midiTicksPerQuarter = 960;

inline juce::MidiFile buildMidiFile (const std::vector<Step>& steps, double quartersPerStep)
{
    const double stepTicks = quartersPerStep * midiTicksPerQuarter;
    const int n = (int) steps.size();

    juce::MidiMessageSequence track;
    auto name = juce::MidiMessage::textMetaEvent (3, "JX Step Sequencer");
    track.addEvent (name, 0.0);

    for (int i = 0; i < n; ++i)
    {
        const Step& s = steps[(size_t) i];
        if (s.t != 'n' || s.numNotes == 0) continue;

        int end = i + 1;
        while (end < n && steps[(size_t) end].t == 't') ++end;

        const double on = i * stepTicks;
        const double off = end * stepTicks;
        for (int k = 0; k < s.numNotes; ++k)
        {
            const int vel = juce::jlimit (1, 127, (int) s.vel[(size_t) k]);
            track.addEvent (juce::MidiMessage::noteOn (1, s.pitch[(size_t) k], (juce::uint8) vel), on);
            track.addEvent (juce::MidiMessage::noteOff (1, s.pitch[(size_t) k], (juce::uint8) 0), off);
        }
    }

    // addEvent keeps insertion order on equal ticks, so a note-off always lands
    // before the next step's note-on and repeated notes retrigger cleanly.
    track.addEvent (juce::MidiMessage::endOfTrack(), n * stepTicks);
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (midiTicksPerQuarter);
    file.addTrack (track);
    return file;
}

inline bool writeMidiFile (const juce::File& dest, const std::vector<Step>& steps, double quartersPerStep)
{
    dest.deleteFile();
    juce::FileOutputStream out (dest);
    if (! out.openedOk()) return false;
    return buildMidiFile (steps, quartersPerStep).writeTo (out, 1);
}

} // namespace jx
