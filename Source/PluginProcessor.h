#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SequencerEngine.h"

#include <atomic>
#include <limits>
#include <vector>

class JXStepSequencerProcessor : public juce::AudioProcessor
{
public:
    // Button presses from the editor. They are queued and handled on the audio
    // thread, because some of them (Clear, REC, PLAY) send note-offs.
    enum Command : int { cmdRec = 1, cmdPlay, cmdRest, cmdTie, cmdClear, cmdPrev, cmdNext, cmdErase };

    JXStepSequencerProcessor();
    ~JXStepSequencerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- editor interface ----
    void pushCommand (Command c);
    jx::DisplayState getDisplay() const;
    juce::AudioParameterChoice& getRateParam() { return *rate; }

    float editorScale = 1.5f;        // remembered with the project

    // Steps per quarter note for each Rate choice: 1/4, 1/8, 1/16, 1/32
    static constexpr double quartersPerStep[4] = { 1.0, 0.5, 0.25, 0.125 };

private:
    jx::SequencerEngine engine;
    mutable juce::SpinLock engineLock;
    juce::AudioParameterChoice* rate = nullptr;

    juce::AbstractFifo commandFifo { 128 };
    std::array<int, 128> commandBuffer {};

    juce::MidiBuffer outMidi;
    juce::int64 lastTickIndex = std::numeric_limits<juce::int64>::min();
    std::atomic<bool> releasePending { false };

    double sampleRate = 44100.0;
    bool hostWasPlaying = false;
    bool firstBlock = true;

    void runCommand (int c);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JXStepSequencerProcessor)
};
