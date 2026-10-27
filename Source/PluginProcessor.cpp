#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    // Collects the engine's notes into the output MIDI buffer at the current
    // sample position. Notes go out on MIDI channel 1, like [noteout].
    struct BufferSink : jx::MidiSink
    {
        juce::MidiBuffer& buffer;
        std::array<int, 128>& sent;
        int samplePos = 0;
        int transpose = 0;
        BufferSink (juce::MidiBuffer& b, std::array<int, 128>& s) : buffer (b), sent (s) {}

        void noteOn (int pitch, int velocity) override
        {
            const int q = juce::jlimit (0, 127, pitch + transpose);
            sent[(size_t) pitch] = q;
            buffer.addEvent (juce::MidiMessage::noteOn (1, q, (juce::uint8) velocity), samplePos);
        }
        void noteOff (int pitch) override
        {
            // the same pitch that was sent at note-on, even if transpose changed since
            buffer.addEvent (juce::MidiMessage::noteOff (1, sent[(size_t) pitch], (juce::uint8) 0), samplePos);
        }
    };
}

JXStepSequencerProcessor::JXStepSequencerProcessor()
#if JucePlugin_IsMidiEffect
    : AudioProcessor (BusesProperties())          // AU MIDI FX (Logic): MIDI only, no audio
#else
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
    addParameter (rate = new juce::AudioParameterChoice (juce::ParameterID { "rate", 1 }, "Rate",
                                                         juce::StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));

    const char* ids[]   = { "rec", "play", "rest", "tie", "clear", "prev", "next", "erase" };
    const char* names[] = { "REC", "PLAY", "Rest", "Tie", "Clear", "Previous step", "Next step", "Erase" };
    for (int i = 0; i < numTriggers; ++i)
        addParameter (triggers[(size_t) i] = new juce::AudioParameterBool (juce::ParameterID { ids[i], 1 }, names[i], false));

    addParameter (transpose = new juce::AudioParameterInt (juce::ParameterID { "transpose", 1 }, "Transpose", -24, 24, 0));
    addParameter (keyTranspose = new juce::AudioParameterBool (juce::ParameterID { "keytranspose", 1 }, "Key transpose", false));
    for (int i = 0; i < 128; ++i) sentPitch[(size_t) i] = i;
}

bool JXStepSequencerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
   #endif
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void JXStepSequencerProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    outMidi.ensureSize (4096);
    firstBlock = true;
}

void JXStepSequencerProcessor::pushCommand (Command c)
{
    const auto scope = commandFifo.write (1);
    if (scope.blockSize1 > 0) commandBuffer[(size_t) scope.startIndex1] = (int) c;
    else if (scope.blockSize2 > 0) commandBuffer[(size_t) scope.startIndex2] = (int) c;
}

void JXStepSequencerProcessor::runCommand (int c)
{
    switch (c)
    {
        case cmdRec:   engine.rectoggle(); break;
        case cmdPlay:  engine.playtoggle(); break;
        case cmdRest:  engine.rest(); break;
        case cmdTie:   engine.tie(); break;
        case cmdClear: engine.clear(); break;
        case cmdPrev:  engine.stepprev(); break;
        case cmdNext:  engine.stepnext(); break;
        case cmdErase: engine.stepdel(); break;
        default:
            if (c > selectBase) engine.selectStep (c - selectBase);
            break;
    }
}

void JXStepSequencerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();                                  // this plugin makes no sound, only MIDI

    // ---- host transport (replaces live.observer is_playing + metro @quantize) ----
    bool hostPlaying = false, havePpq = false;
    double ppq = 0.0, bpm = 120.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { ppq = *p; havePpq = true; }
            if (auto b = pos->getBpm()) if (*b > 0.0) bpm = *b;
        }
    }

    outMidi.clear();
    BufferSink sink (outMidi, sentPitch);
    sink.transpose = transpose->get();

    const juce::SpinLock::ScopedLockType lock (engineLock);
    engine.out = &sink;

    if (firstBlock || hostPlaying != hostWasPlaying)
    {
        engine.reset();                              // transport started/stopped: silence, back to step 1
        lastTickIndex = std::numeric_limits<juce::int64>::min();
    }
    hostWasPlaying = hostPlaying;
    firstBlock = false;

    if (releasePending.exchange (false))
        engine.releaseAll();                         // a project/preset was loaded

    // ---- button presses from the editor ----
    {
        const auto scope = commandFifo.read (commandFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) runCommand (commandBuffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) runCommand (commandBuffer[(size_t) (scope.startIndex2 + i)]);
    }

    // ---- MIDI-mapped buttons: a press triggers the same command as a click ----
    for (int i = 0; i < numTriggers; ++i)
    {
        const bool on = triggers[(size_t) i]->get();
        if (on && ! triggerWasOn[(size_t) i]) runCommand (cmdRec + i);
        triggerWasOn[(size_t) i] = on;
    }

    // ---- clock ticks that fall inside this block, on the Rate grid ----
    constexpr int maxTicks = 64;
    int tickPos[maxTicks];
    int numTicks = 0;
    if (hostPlaying && havePpq && numSamples > 0)
    {
        const double stepLen = quartersPerStep[juce::jlimit (0, 3, rate->getIndex())];
        const double quartersPerSample = bpm / 60.0 / sampleRate;
        const double endPpq = ppq + numSamples * quartersPerSample;
        auto k = (juce::int64) std::ceil (ppq / stepLen - 1.0e-7);
        for (double b = (double) k * stepLen; b < endPpq && numTicks < maxTicks; b = (double) (++k) * stepLen)
        {
            if (k == lastTickIndex) continue;        // already fired at the end of the previous block
            lastTickIndex = k;
            const int offset = (int) std::floor ((b - ppq) / quartersPerSample + 0.5);
            tickPos[numTicks++] = juce::jlimit (0, numSamples - 1, offset);
        }
    }

    // ---- MIDI input and ticks, in time order ----
    int t = 0;
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int samplePos = juce::jlimit (0, juce::jmax (0, numSamples - 1), metadata.samplePosition);

        while (t < numTicks && tickPos[t] < samplePos) { sink.samplePos = tickPos[t++]; engine.tick(); }

        sink.samplePos = samplePos;

        // KEY: while the sequence plays, a key sets the transpose (C3 = 0) and is not passed on
        if (keyTranspose->get() && engine.isPlaying() && ! engine.isWriting() && (msg.isNoteOn() || msg.isNoteOff()))
        {
            if (msg.isNoteOn())
            {
                const int t = juce::jlimit (-24, 24, msg.getNoteNumber() - 60);
                *transpose = t;
                sink.transpose = t;
            }
            continue;
        }

        outMidi.addEvent (msg, samplePos);           // pass the input through (notein -> noteout)
        if (msg.isNoteOn())       engine.note (msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff()) engine.note (msg.getNoteNumber(), 0);
    }
    while (t < numTicks) { sink.samplePos = tickPos[t++]; engine.tick(); }

    engine.out = nullptr;
    midi.swapWith (outMidi);
}

jx::DisplayState JXStepSequencerProcessor::getDisplay() const
{
    const juce::SpinLock::ScopedLockType lock (engineLock);
    return engine.getDisplay();
}

std::vector<jx::Step> JXStepSequencerProcessor::getSequence() const
{
    std::vector<jx::Step> steps;
    steps.reserve (jx::MAXSTEPS);
    const juce::SpinLock::ScopedLockType lock (engineLock);
    for (int i = 0; i < engine.getNumSteps(); ++i) steps.push_back (engine.getStep (i));
    return steps;
}

// ---- state: replaces the coll @embed 1 ----

void JXStepSequencerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("JXStepSequencer");
    root.setAttribute ("version", 1);
    root.setAttribute ("rate", rate->getIndex());
    root.setAttribute ("scale", (double) editorScale);
    root.setAttribute ("help", showHelp.load());
    root.setAttribute ("transpose", transpose->get());
    root.setAttribute ("key", keyTranspose->get());

    auto* seq = root.createNewChildElement ("Sequence");
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        for (int i = 0; i < engine.getNumSteps(); ++i)
        {
            const auto& s = engine.getStep (i);
            auto* e = seq->createNewChildElement ("Step");
            e->setAttribute ("t", juce::String::charToString ((juce::juce_wchar) s.t));
            juce::String notes;
            for (int n = 0; n < s.numNotes; ++n)
                notes << (int) s.pitch[(size_t) n] << " " << (int) s.vel[(size_t) n] << " ";
            if (s.numNotes > 0) e->setAttribute ("notes", notes.trim());
        }
    }
    copyXmlToBinary (root, destData);
}

void JXStepSequencerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("JXStepSequencer")) return;

    *rate = juce::jlimit (0, 3, xml->getIntAttribute ("rate", 2));
    editorScale = (float) juce::jlimit (1.0, 3.0, xml->getDoubleAttribute ("scale", 1.5));
    showHelp = xml->getBoolAttribute ("help", false);
    *transpose = juce::jlimit (-24, 24, xml->getIntAttribute ("transpose", 0));
    *keyTranspose = xml->getBoolAttribute ("key", false);

    std::vector<jx::Step> loaded;
    if (auto* seq = xml->getChildByName ("Sequence"))
    {
        for (auto* e : seq->getChildWithTagNameIterator ("Step"))
        {
            jx::Step s;
            const auto t = e->getStringAttribute ("t", "r");
            s.t = t.isNotEmpty() ? (char) t[0] : 'r';
            juce::StringArray tokens;
            tokens.addTokens (e->getStringAttribute ("notes"), " ", "");
            tokens.removeEmptyStrings();
            for (int i = 0; i + 1 < tokens.size() && s.numNotes < jx::MAXNOTES; i += 2)
            {
                s.pitch[(size_t) s.numNotes] = (juce::uint8) juce::jlimit (0, 127, tokens[i].getIntValue());
                s.vel[(size_t) s.numNotes]   = (juce::uint8) juce::jlimit (0, 127, tokens[i + 1].getIntValue());
                s.numNotes++;
            }
            loaded.push_back (s);
        }
    }

    const juce::SpinLock::ScopedLockType lock (engineLock);
    engine.out = nullptr;
    engine.restoreBegin();
    releasePending = true;   // notes still sounding get their note-offs in the next block
    for (const auto& s : loaded) engine.restore (s);
}

juce::AudioProcessorEditor* JXStepSequencerProcessor::createEditor()
{
    return new JXStepSequencerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JXStepSequencerProcessor();
}
