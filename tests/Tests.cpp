#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MidiExport.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; failures++; } } while (0)

static std::string cells (const jx::DisplayState& d)
{ return std::string (d.cells.data(), (size_t) d.count); }

struct Rec : jx::MidiSink
{
    std::vector<std::string> ev;
    void noteOn (int p, int v) override { ev.push_back ("on " + std::to_string (p) + " " + std::to_string (v)); }
    void noteOff (int p) override { ev.push_back ("off " + std::to_string (p)); }
};

static void engineTests()
{
    jx::SequencerEngine e; Rec r; e.out = &r;
    e.tie(); e.rest();                                   // not writing: ignored
    CHECK (e.getNumSteps() == 0);
    e.rectoggle();
    e.tie();                                             // nothing to tie to
    CHECK (e.getNumSteps() == 0);
    e.note (60, 100); e.note (64, 90); e.note (67, 80);
    CHECK (jx::infoText (e.getDisplay().info) == "C3 E3 G3");
    e.note (60, 0); e.note (64, 0);
    CHECK (e.getNumSteps() == 0);                        // committed only when all keys are up
    e.note (67, 0);
    CHECK (e.getNumSteps() == 1);
    e.tie(); e.rest();
    e.tie();                                             // previous is rest: ignored
    e.note (62, 100); e.note (62, 0);
    CHECK (cells (e.getDisplay()) == "ntrn");
    CHECK (e.getDisplay().wr == 5);
    e.stepdel();                                         // backspace while recording
    CHECK (cells (e.getDisplay()) == "ntr");
    e.note (62, 100); e.note (62, 0);
    e.rectoggle();
    auto d = e.getDisplay();
    CHECK (! d.rec && d.sel == 1 && d.wr == 0);

    // 7 notes: only 6 kept
    jx::SequencerEngine e2; e2.rectoggle();
    for (int p = 60; p < 67; ++p) e2.note (p, 100);
    for (int p = 60; p < 67; ++p) e2.note (p, 0);
    CHECK (e2.getStep (0).numNotes == 6);

    // playback
    e.playtoggle();
    e.tick(); e.tick(); e.tick(); e.tick(); e.tick();
    std::string all; for (auto& s : r.ev) all += s + ",";
    CHECK (all == "on 60 100,on 64 90,on 67 80,off 60,off 64,off 67,on 62 100,off 62,on 60 100,on 64 90,on 67 80,");
    CHECK (e.getDisplay().cur == 1);

    // erase blanks step 1 and its tie, then a chord refills the blank step
    r.ev.clear();
    e.playtoggle();
    CHECK (r.ev.size() == 3);                            // stopping play released the C-E-G chord
    e.stepdel();
    CHECK (cells (e.getDisplay()) == "rrrn");
    e.note (48, 70);
    e.note (48, 0);
    CHECK (cells (e.getDisplay()) == "nrrn" && e.getStep (0).pitch[0] == 48);
    e.stepnext(); e.stepnext(); e.stepnext();
    CHECK (e.getDisplay().sel == 4);
    e.note (50, 70); e.note (50, 0);                     // step 4 not blank: ignored
    CHECK (e.getStep (3).pitch[0] == 62);
    e.stepnext();
    CHECK (e.getDisplay().sel == 1);                     // wraps
    e.stepprev();
    CHECK (e.getDisplay().sel == 4);

    // full at 128
    jx::SequencerEngine e3; e3.rectoggle();
    for (int i = 0; i < 130; ++i) e3.rest();
    CHECK (e3.getNumSteps() == 128 && ! e3.isWriting());
    // click-select
    { jx::SequencerEngine e5; e5.rectoggle(); e5.rest(); e5.rest(); e5.rest(); e5.selectStep (2); CHECK (e5.getDisplay().sel == 0);  // not while recording
      e5.rectoggle(); e5.selectStep (3); CHECK (e5.getDisplay().sel == 3); e5.selectStep (9); CHECK (e5.getDisplay().sel == 3); }
    // REC off: Rest / Tie act on the selected step
    { jx::SequencerEngine e6; e6.rectoggle(); e6.note (60,100); e6.note (60,0); e6.note (62,100); e6.note (62,0); e6.note (64,100); e6.note (64,0); e6.rectoggle();
      e6.selectStep (2); e6.tie(); CHECK (cells (e6.getDisplay()) == "ntn");
      e6.selectStep (1); e6.rest(); CHECK (cells (e6.getDisplay()) == "rrn");      // its tie goes too
      e6.selectStep (2); e6.tie(); CHECK (cells (e6.getDisplay()) == "rrn");       // nothing to tie to
      e6.selectStep (1); e6.tie(); CHECK (cells (e6.getDisplay()) == "rrn"); }
    std::cout << "engine tests done\n";
}

struct FakeHead : juce::AudioPlayHead
{
    bool playing = true; double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setIsPlaying (playing); p.setPpqPosition (ppq); p.setBpm (bpm); return p;
    }
};

static void processorTests()
{
    JXStepSequencerProcessor proc;
    FakeHead head; proc.setPlayHead (&head);
    const double sr = 48000; const int bs = 480;   // 1/16 at 120bpm = 6000 samples
    proc.setPlayConfigDetails (0, 2, sr, bs);
    proc.prepareToPlay (sr, bs);
    juce::AudioBuffer<float> audio (2, bs);

    long long t = 0;
    std::vector<std::pair<long long, juce::MidiMessage>> outEvents;
    auto run = [&] (juce::MidiBuffer in, int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer m; if (b == 0) m = in;
            if (head.playing) head.ppq = (double) t / sr * head.bpm / 60.0;
            proc.processBlock (audio, m);
            for (auto md : m) outEvents.push_back ({ t + md.samplePosition, md.getMessage() });
            t += bs;
        }
    };

    head.playing = false;
    proc.pushCommand (JXStepSequencerProcessor::cmdRec);
    juce::MidiBuffer in;
    in.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 10);
    in.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 20);
    in.addEvent (juce::MidiMessage::noteOff (1, 60), 100);
    in.addEvent (juce::MidiMessage::noteOff (1, 64), 110);
    run (in, 1);
    proc.pushCommand (JXStepSequencerProcessor::cmdTie);
    proc.pushCommand (JXStepSequencerProcessor::cmdRest);
    proc.pushCommand (JXStepSequencerProcessor::cmdRec);
    run ({}, 1);
    CHECK (cells (proc.getDisplay()) == "ntr");
    CHECK (outEvents.size() == 4);                       // input passed through
    outEvents.clear();

    // state round trip
    juce::MemoryBlock mb; proc.getStateInformation (mb);
    JXStepSequencerProcessor proc2; proc2.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (cells (proc2.getDisplay()) == "ntr" && proc2.getRateParam().getIndex() == 2);

    // play with transport running
    proc.pushCommand (JXStepSequencerProcessor::cmdPlay);
    head.playing = true; t = 0;
    run ({}, 100);                                       // 1 second = 8 steps of 1/16
    std::vector<long long> ons, offs;
    for (auto& [time, msg] : outEvents) (msg.isNoteOn() ? ons : offs).push_back (time);
    // pattern n t r : note-on at steps 0, 3, 6 -> samples 0, 18000, 36000 (x2 notes)
    CHECK (ons.size() == 6);
    if (ons.size() == 6) CHECK (ons[0] == 0 && ons[2] == 18000 && ons[4] == 36000);
    // off at the rest steps 2, 5 -> 12000, 30000
    CHECK (offs.size() == 4);
    if (offs.size() == 4) CHECK (offs[0] == 12000 && offs[2] == 30000);

    // transport stop releases sounding notes (step 6 is sounding)
    outEvents.clear();
    head.playing = false; run ({}, 1);
    CHECK (outEvents.size() == 2 && outEvents[0].second.isNoteOff());

    // block boundary exactly on a step: no double tick
    JXStepSequencerProcessor p3; FakeHead h3; p3.setPlayHead (&h3);
    p3.prepareToPlay (sr, 6000); juce::AudioBuffer<float> a3 (2, 6000);
    p3.pushCommand (JXStepSequencerProcessor::cmdRec);
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 70, (juce::uint8) 1), 0); m.addEvent (juce::MidiMessage::noteOff (1, 70), 1); h3.playing = false; p3.processBlock (a3, m); }
    p3.pushCommand (JXStepSequencerProcessor::cmdRec);
    p3.pushCommand (JXStepSequencerProcessor::cmdPlay);
    h3.playing = true;
    int count = 0;
    for (int b = 0; b < 8; ++b)
    {
        h3.ppq = b * 0.25 - (b % 2 ? 1e-12 : 0.0);       // jitter around the boundary
        juce::MidiBuffer m; p3.processBlock (a3, m);
        for (auto md : m) if (md.getMessage().isNoteOn()) count++;
    }
    CHECK (count == 8);
    // MIDI-mapped trigger parameters: press = one command, release does nothing
    {
        JXStepSequencerProcessor p4; FakeHead h4; h4.playing = false; p4.setPlayHead (&h4);
        p4.prepareToPlay (sr, bs); juce::AudioBuffer<float> a4 (2, bs);
        auto* recParam = p4.getParameters()[1];
        CHECK (recParam->getName (20) == "REC");
        recParam->setValue (1.0f); { juce::MidiBuffer m; p4.processBlock (a4, m); }
        CHECK (p4.getDisplay().rec);
        { juce::MidiBuffer m; p4.processBlock (a4, m); }
        CHECK (p4.getDisplay().rec);                 // held: no second toggle
        recParam->setValue (0.0f); { juce::MidiBuffer m; p4.processBlock (a4, m); }
        CHECK (p4.getDisplay().rec);
        recParam->setValue (1.0f); { juce::MidiBuffer m; p4.processBlock (a4, m); }
        CHECK (! p4.getDisplay().rec);               // second press toggles off
    }
    // transpose + KEY
    {
        JXStepSequencerProcessor p5; FakeHead h5; h5.playing = false; p5.setPlayHead (&h5);
        p5.prepareToPlay (sr, 6000); juce::AudioBuffer<float> a5 (2, 6000);
        p5.pushCommand (JXStepSequencerProcessor::cmdRec);
        { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOff (1, 60), 1); p5.processBlock (a5, m); }
        p5.pushCommand (JXStepSequencerProcessor::cmdRec);
        p5.pushCommand (JXStepSequencerProcessor::cmdPlay);
        p5.getTransposeParam() = 5;
        h5.playing = true; std::vector<int> ons, offs;
        auto blk = [&] (double ppq, juce::MidiBuffer in) { h5.ppq = ppq; p5.processBlock (a5, in); for (auto md : in) { auto mm = md.getMessage(); if (mm.isNoteOn()) ons.push_back (mm.getNoteNumber()); else if (mm.isNoteOff()) offs.push_back (mm.getNoteNumber()); } };
        blk (0.0, {});
        CHECK (ons.size() == 1 && ons[0] == 65);
        p5.getTransposeParam() = -2;                      // change while the note sounds
        blk (0.25, {});
        CHECK (offs.size() == 1 && offs[0] == 65);         // note-off matches what was sent
        CHECK (ons.size() == 2 && ons[1] == 58);
        p5.getKeyParam() = true; ons.clear();
        juce::MidiBuffer kin; kin.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 10);
        blk (0.5, kin);
        CHECK (p5.getTransposeParam().get() == 4);         // E3 = +4
        CHECK (ons.size() == 1 && ons[0] == 58);           // key not passed through (step on this tick came first)
        ons.clear(); blk (0.75, {});
        CHECK (ons.size() == 1 && ons[0] == 64);           // next step plays at +4
    }
    std::cout << "processor tests done\n";
}

static void snapshot (const char* path, std::function<void (JXStepSequencerProcessor&)> setup)
{
    JXStepSequencerProcessor proc; FakeHead head; head.playing = false;
    proc.setPlayHead (&head); proc.prepareToPlay (48000, 480);
    setup (proc);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 3.0f);
    juce::File f (path); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
}

static void midiExportTests()
{
    // C-E chord, tie, rest, D, D (repeated note must retrigger)
    std::vector<jx::Step> steps (5);
    steps[0].t = 'n'; steps[0].numNotes = 2; steps[0].pitch = { 60, 64 }; steps[0].vel = { 100, 90 };
    steps[1].t = 't';
    steps[2].t = 'r';
    steps[3].t = 'n'; steps[3].numNotes = 1; steps[3].pitch = { 62 }; steps[3].vel = { 80 };
    steps[4] = steps[3];

    auto file = jx::buildMidiFile (steps, 0.25);            // 1/16 = 240 ticks
    CHECK (file.getTimeFormat() == 960);
    const auto* t = file.getTrack (0);
    std::vector<std::string> ev;
    for (int i = 0; i < t->getNumEvents(); ++i)
    {
        const auto& m = t->getEventPointer (i)->message;
        if (m.isNoteOn()) ev.push_back ("on " + std::to_string (m.getNoteNumber()) + "@" + std::to_string ((int) m.getTimeStamp()));
        else if (m.isNoteOff()) ev.push_back ("off " + std::to_string (m.getNoteNumber()) + "@" + std::to_string ((int) m.getTimeStamp()));
    }
    std::string all; for (auto& e : ev) all += e + ",";
    CHECK (all == "on 60@0,on 64@0,off 60@480,off 64@480,on 62@720,off 62@960,on 62@960,off 62@1200,");
    if (all != "on 60@0,on 64@0,off 60@480,off 64@480,on 62@720,off 62@960,on 62@960,off 62@1200,") std::cout << all << "\n";

    // round trip through a real file
    auto f = juce::File::createTempFile (".mid");
    CHECK (jx::writeMidiFile (f, steps, 0.25));
    juce::FileInputStream in (f);
    juce::MidiFile back; CHECK (back.readFrom (in));
    CHECK (back.getNumTracks() == 1);
    f.deleteFile();
    std::cout << "midi export tests done\n";
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    engineTests();
    midiExportTests();
    processorTests();

    auto feed = [] (JXStepSequencerProcessor& p, std::initializer_list<std::vector<int>> chords, std::vector<int> cmds)
    {
        juce::AudioBuffer<float> a (2, 480);
        p.pushCommand (JXStepSequencerProcessor::cmdRec);
        size_t ci = 0;
        for (auto& ch : chords)
        {
            juce::MidiBuffer m;
            if (ch.empty()) { p.pushCommand ((JXStepSequencerProcessor::Command) cmds[ci++]); }
            for (int n : ch) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
            for (int n : ch) m.addEvent (juce::MidiMessage::noteOff (1, n), 5);
            p.processBlock (a, m);
        }
        juce::MidiBuffer m; p.processBlock (a, m);
    };
    const char* dir = getenv ("SNAPDIR");
    std::string base = dir ? dir : ".";
    snapshot ((base + "/ui_recording.png").c_str(), [&] (auto& p) {
        feed (p, { {60,64,67}, {}, {62,65,69}, {}, {55,59,62,65} }, { JXStepSequencerProcessor::cmdTie, JXStepSequencerProcessor::cmdRest });
    });
    snapshot ((base + "/ui_playing.png").c_str(), [&] (auto& p) {
        feed (p, { {60,64,67}, {}, {62}, {}, {55,59,62,65}, {57} }, { JXStepSequencerProcessor::cmdTie, JXStepSequencerProcessor::cmdRest });
        p.pushCommand (JXStepSequencerProcessor::cmdRec);
        p.pushCommand (JXStepSequencerProcessor::cmdPlay);
        FakeHead h; h.playing = true; p.setPlayHead (&h);
        juce::AudioBuffer<float> a (2, 6000);
        for (int b = 0; b < 3; ++b) { h.ppq = b * 0.25; juce::MidiBuffer m; p.processBlock (a, m); }
        p.setPlayHead (nullptr);
    });


    JXStepSequencerEditor::amxdLook = true;
    snapshot ((base + "/amxd_device.png").c_str(), [&] (auto& p) {
        feed (p, { {48,55,60,64}, {}, {53,57,60,65}, {}, {55,59,62,67}, {57,60,64,69}, {50,57,62,65}, {}, {52,55,59,64}, {55,59,62,65} },
              { JXStepSequencerProcessor::cmdTie, JXStepSequencerProcessor::cmdRest, JXStepSequencerProcessor::cmdTie });
        p.pushCommand (JXStepSequencerProcessor::cmdRec);
        p.pushCommand (JXStepSequencerProcessor::cmdPlay);
        FakeHead h; h.playing = true; p.setPlayHead (&h);
        juce::AudioBuffer<float> a (2, 6000);
        for (int b = 0; b < 5; ++b) { h.ppq = b * 0.25; juce::MidiBuffer m; p.processBlock (a, m); }
        p.setPlayHead (nullptr);
    });
    JXStepSequencerEditor::amxdLook = false;

    {   // transpose UI previews
        JXStepSequencerProcessor p; FakeHead head; head.playing = false; p.setPlayHead (&head); p.prepareToPlay (48000, 480);
        feed (p, { {60,64,67}, {}, {62}, {}, {55,59,62,65}, {57} }, { JXStepSequencerProcessor::cmdTie, JXStepSequencerProcessor::cmdRest });
        p.pushCommand (JXStepSequencerProcessor::cmdRec);
        { juce::AudioBuffer<float> a (2, 480); juce::MidiBuffer m; p.processBlock (a, m); }
        p.getTransposeParam() = 5; p.getKeyParam() = true;
        std::unique_ptr<JXStepSequencerEditor> ed (dynamic_cast<JXStepSequencerEditor*> (p.createEditor()));
        auto save = [&] (const char* n) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 3.0f); juce::File f (base + n); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os); };
        save ("/trn_closed.png");
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
