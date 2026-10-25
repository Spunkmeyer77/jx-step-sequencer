// SequencerEngine.h  --  JX-3P style polyphonic step sequencer engine
//
// A line-by-line port of jxseq.js from the Max for Live device
// "JX Step Sequencer". Plain C++ with no JUCE dependency and no memory
// allocation, so it is safe to run on the audio thread and easy to test.
//
// Behaviour (same as the M4L device):
//   * REC (write) starts a fresh sequence at step 1. Each chord you play
//     becomes one step (committed when every key is released, max 6 notes).
//   * Rest adds a silent step, Tie holds the previous step through a new one.
//   * When not recording, < and > select a step; Erase blanks it (and any
//     ties that continued it). A blank selected step takes the next chord.
//   * While recording, Erase works like backspace.
//   * PLAY arms playback; one clock tick = one step. Host transport
//     start/stop silences and returns to step 1.

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace jx
{

constexpr int MAXSTEPS = 128;
constexpr int MAXNOTES = 6;

struct Step
{
    char t = 'r';                  // 'n' = notes, 'r' = rest, 't' = tie
    int numNotes = 0;
    std::array<uint8_t, MAXNOTES> pitch {};
    std::array<uint8_t, MAXNOTES> vel {};
};

// What the big display shows as the "chord" text (jxseq.js lastInfo).
struct Info
{
    enum Kind : uint8_t { Notes, Rest, Tie, Full };
    Kind kind = Notes;             // Notes with numNotes == 0 shows "-"
    int numNotes = 0;
    std::array<uint8_t, MAXNOTES> pitch {};
};

// Everything the screen needs, copied to the UI thread (plain data only).
struct DisplayState
{
    std::array<char, MAXSTEPS> cells {};
    std::array<bool, MAXSTEPS> chord {};   // note step with 2 or more notes
    int count = 0;
    int cur = 0;                   // playing step, 1-based, 0 = none
    int wr = 0;                    // next step to be written, 0 = not recording
    int sel = 0;                   // selected step, 0 = none
    bool rec = false;
    bool play = false;
    Info info;

    bool operator== (const DisplayState& o) const
    {
        if (count != o.count || cur != o.cur || wr != o.wr || sel != o.sel
            || rec != o.rec || play != o.play || info.kind != o.info.kind
            || info.numNotes != o.info.numNotes)
            return false;
        for (int i = 0; i < count; ++i)
            if (cells[(size_t) i] != o.cells[(size_t) i] || chord[(size_t) i] != o.chord[(size_t) i]) return false;
        for (int i = 0; i < info.numNotes; ++i) if (info.pitch[(size_t) i] != o.info.pitch[(size_t) i]) return false;
        return true;
    }
    bool operator!= (const DisplayState& o) const { return ! (*this == o); }
};

struct MidiSink
{
    virtual ~MidiSink() = default;
    virtual void noteOn (int pitch, int velocity) = 0;
    virtual void noteOff (int pitch) = 0;
};

inline std::string noteName (int p)
{
    static const char* NAMES[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string (NAMES[p % 12]) + std::to_string (p / 12 - 2);   // Live naming: 60 = C3
}

inline std::string infoText (const Info& info)
{
    switch (info.kind)
    {
        case Info::Rest: return "REST";
        case Info::Tie:  return "TIE";
        case Info::Full: return "FULL";
        case Info::Notes: break;
    }
    if (info.numNotes == 0) return "-";
    std::string s;
    for (int i = 0; i < info.numNotes; ++i)
    {
        if (i > 0) s += " ";
        s += noteName (info.pitch[(size_t) i]);
    }
    return s;
}

class SequencerEngine
{
public:
    MidiSink* out = nullptr;       // set by the processor for each block

    // ---------- messages in (same names as jxseq.js) ----------

    void note (int p, int v)
    {
        if (p < 0 || p > 127) return;
        if (v > 0)
        {
            if (! writing && ! editing)
            {
                if (! editTarget()) return;   // only a selected blank step takes notes when not recording
                editing = true;
                resetEntry();
            }
            if (! held[(size_t) p]) { held[(size_t) p] = true; heldCount++; }
            if (chord.numNotes < MAXNOTES)
            {
                bool dup = false;
                for (int i = 0; i < chord.numNotes; ++i) if (chord.pitch[(size_t) i] == p) dup = true;
                if (! dup) addNote (chord, p, v);
            }
            showInfoNotes (chord);
        }
        else
        {
            if (held[(size_t) p]) { held[(size_t) p] = false; heldCount--; }
            if (heldCount == 0 && chord.numNotes > 0)
            {
                if (writing) commitChord();
                else if (editing) commitEdit();
            }
            if (! writing && heldCount == 0 && chord.numNotes == 0) editing = false;
        }
    }

    void write (bool on)
    {
        if (on == writing) return;
        writing = on;
        editing = false;
        resetEntry();
        if (on)                       // like the hardware: Write starts a fresh sequence at step 1
        {
            numSteps = 0;
            sel = 0;
            releaseAll();
            pos = -1;
            showInfoNotes (Step {});  // "-"
        }
        refresh();
    }

    void rest()
    {
        if (! writing || chord.numNotes > 0 || heldCount > 0) return;
        showInfoKind (Info::Rest);
        pushStep (makeStep ('r'));
    }

    void tie()
    {
        if (! writing || chord.numNotes > 0 || heldCount > 0) return;
        if (numSteps == 0) return;
        if (steps[(size_t) numSteps - 1].t == 'r') return;   // nothing to tie to
        showInfoKind (Info::Tie);
        pushStep (makeStep ('t'));
    }

    void clear()
    {
        releaseAll();
        numSteps = 0;
        sel = 0;
        pos = -1;
        editing = false;
        resetEntry();
        showInfoNotes (Step {});
        refresh();
    }

    void stepnext() { moveSel (1); }

    void selectStep (int idx)        // 1-based; clicking a step cell on the screen
    {
        if (writing || idx < 1 || idx > numSteps) return;
        if (editing) { editing = false; resetEntry(); }
        sel = idx;
        if (! playing) showStepInfo (steps[(size_t) sel - 1]);
        refresh();
    }
    void stepprev() { moveSel (-1); }

    void stepdel()
    {
        if (numSteps == 0) return;
        if (writing)                  // while recording, erase works like backspace
        {
            resetEntry();
            numSteps--;
            if (numSteps == 0) showInfoNotes (Step {});
            else showStepInfo (steps[(size_t) numSteps - 1]);
            refresh();
            return;
        }
        if (editing) { editing = false; resetEntry(); }
        clampSel();
        const int idx = sel - 1;
        if (steps[(size_t) idx].t == 'r') return;           // already blank
        steps[(size_t) idx] = makeStep ('r');                // blank the step, length stays
        for (int j = idx + 1; j < numSteps && steps[(size_t) j].t == 't'; ++j)
            steps[(size_t) j] = makeStep ('r');              // ties that continued it have nothing to hold
        if (! playing) showInfoKind (Info::Rest);
        refresh();
    }

    void play (bool on)
    {
        playing = on;
        pos = -1;
        if (! on) releaseAll();
        refresh();
    }

    void rectoggle()  { write (! writing); }     // REC button
    void playtoggle() { play (! playing); }      // PLAY button

    void reset()                     // host transport started or stopped
    {
        releaseAll();
        pos = -1;
    }

    void tick()                      // one clock tick = one step
    {
        if (! playing || numSteps == 0) return;
        const int next = (pos + 1) % numSteps;
        const Step& s = steps[(size_t) next];
        if (s.t != 't')
        {
            releaseAll();
            if (s.t == 'n')
            {
                for (int i = 0; i < s.numNotes; ++i)
                {
                    emitOn (s.pitch[(size_t) i], s.vel[(size_t) i]);
                    sounding[(size_t) numSounding++] = s.pitch[(size_t) i];
                }
            }
        }
        pos = next;
        showStepInfo (s);
    }

    void releaseAll()
    {
        for (int i = 0; i < numSounding; ++i) emitOff (sounding[(size_t) i]);
        numSounding = 0;
    }

    // ---------- storage (replaces the embedded coll) ----------

    int getNumSteps() const { return numSteps; }
    const Step& getStep (int i) const { return steps[(size_t) i]; }

    void restoreBegin()              // notes still sounding stay tracked, so a later releaseAll() stops them
    {
        numSteps = 0;
        pos = -1;
        sel = 0;
        editing = false;
        resetEntry();
        showInfoNotes (Step {});
    }

    void restore (const Step& s)     // jxseq.js restore(): append one stored step
    {
        if (numSteps < MAXSTEPS) steps[(size_t) numSteps++] = sanitise (s);
        refresh();
    }

    // ---------- display ----------

    DisplayState getDisplay() const
    {
        DisplayState d;
        d.count = numSteps;
        for (int i = 0; i < numSteps; ++i)
        {
            d.cells[(size_t) i] = steps[(size_t) i].t;
            d.chord[(size_t) i] = steps[(size_t) i].t == 'n' && steps[(size_t) i].numNotes > 1;
        }
        d.cur = playing ? pos + 1 : 0;
        d.wr = writing ? numSteps + 1 : 0;
        d.rec = writing;
        d.play = playing;
        d.sel = writing ? 0 : sel;
        d.info = lastInfo;
        return d;
    }

    bool isWriting() const { return writing; }
    bool isPlaying() const { return playing; }

private:
    std::array<Step, MAXSTEPS> steps {};
    int numSteps = 0;
    bool writing = false;
    bool playing = false;
    int pos = -1;
    std::array<bool, 128> held {};
    int heldCount = 0;
    Step chord;                      // notes of the step being entered
    std::array<uint8_t, 16> sounding {};
    int numSounding = 0;
    Info lastInfo;
    bool editing = false;
    int sel = 0;                     // 1-based, 0 = none

    static Step makeStep (char t) { Step s; s.t = t; return s; }

    static Step sanitise (Step s)
    {
        if (s.t != 'n' && s.t != 'r' && s.t != 't') s.t = 'r';
        if (s.numNotes < 0) s.numNotes = 0;
        if (s.numNotes > MAXNOTES) s.numNotes = MAXNOTES;
        if (s.t != 'n') s.numNotes = 0;
        for (int i = 0; i < s.numNotes; ++i)
        {
            s.pitch[(size_t) i] = (uint8_t) (s.pitch[(size_t) i] & 127);
            s.vel[(size_t) i]   = (uint8_t) (s.vel[(size_t) i] & 127);
        }
        return s;
    }

    static void addNote (Step& s, int p, int v)
    {
        s.pitch[(size_t) s.numNotes] = (uint8_t) p;
        s.vel[(size_t) s.numNotes] = (uint8_t) (v > 127 ? 127 : v);
        s.numNotes++;
    }

    void emitOn (int p, int v) { if (out != nullptr) out->noteOn (p, v); }
    void emitOff (int p)       { if (out != nullptr) out->noteOff (p); }

    void showInfoNotes (const Step& s)
    {
        lastInfo.kind = Info::Notes;
        lastInfo.numNotes = s.numNotes;
        lastInfo.pitch = s.pitch;
    }
    void showInfoKind (Info::Kind k) { lastInfo.kind = k; lastInfo.numNotes = 0; }
    void showStepInfo (const Step& s)
    {
        if (s.t == 'n') showInfoNotes (s);
        else showInfoKind (s.t == 'r' ? Info::Rest : Info::Tie);
    }

    void clampSel()
    {
        if (numSteps == 0) sel = 0;
        else if (sel < 1) sel = 1;
        else if (sel > numSteps) sel = numSteps;
    }

    void refresh() { clampSel(); }   // the display is read on demand via getDisplay()

    void resetEntry()
    {
        held.fill (false);
        heldCount = 0;
        chord = Step {};
    }

    void pushStep (const Step& step)
    {
        if (numSteps >= MAXSTEPS) { writing = false; showInfoKind (Info::Full); return; }
        steps[(size_t) numSteps++] = step;
        if (numSteps >= MAXSTEPS) writing = false;
        refresh();
    }

    bool editTarget() const          // a blank (rest) step is selected and we are not recording
    {
        return ! writing && numSteps > 0 && sel >= 1 && sel <= numSteps && steps[(size_t) sel - 1].t == 'r';
    }

    void commitEdit()                // the chord played into the selected blank step
    {
        Step c = chord;
        chord = Step {};
        editing = false;
        if (c.numNotes == 0 || ! editTarget()) return;
        c.t = 'n';
        steps[(size_t) sel - 1] = c;
        showInfoNotes (c);
        refresh();
    }

    void commitChord()
    {
        if (chord.numNotes == 0) return;
        Step c = chord;
        chord = Step {};
        c.t = 'n';
        showInfoNotes (c);
        pushStep (c);
    }

    void moveSel (int dir)
    {
        if (writing || numSteps == 0) return;
        if (editing) { editing = false; resetEntry(); }
        clampSel();
        sel += dir;
        if (sel > numSteps) sel = 1;
        if (sel < 1) sel = numSteps;
        if (! playing) showStepInfo (steps[(size_t) sel - 1]);
        refresh();
    }
};

} // namespace jx
