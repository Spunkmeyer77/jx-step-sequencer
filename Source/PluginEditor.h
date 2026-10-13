#pragma once

#include "PluginProcessor.h"

// The plugin window. Everything is drawn in the M4L device's own coordinates
// (357 x 169) and scaled, so the layout matches the presentation view exactly.
class JXStepSequencerEditor : public juce::AudioProcessorEditor,
                              public juce::TooltipClient,
                              private juce::Timer
{
public:
    explicit JXStepSequencerEditor (JXStepSequencerProcessor&);
    ~JXStepSequencerEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    static constexpr int baseWidth = 357;
    static constexpr int baseHeight = 169;

private:
    enum class Kind { Text, Round, Tab };

    struct Control
    {
        juce::String id;
        Kind kind;
        juce::Rectangle<float> r;            // device coordinates
        JXStepSequencerProcessor::Command cmd;
        juce::Colour bg, bgOn, text, textOn;
        juce::String label;
        juce::String hint;                   // tooltip: the device's annotation text
        double flashUntil = 0.0;             // live.button blinks when pressed
    };

    JXStepSequencerProcessor& proc;
    std::vector<Control> controls;
    jx::DisplayState display;
    int shownRate = -1;
    int hover = -1;                          // index into controls, 100 = Rate tab
    juce::TooltipWindow tooltipWindow { this, 700 };

    void timerCallback() override;
    float scale() const;
    juce::Point<float> toDevice (juce::Point<float> p) const;
    int findControl (juce::Point<float> devicePos) const;
    int hitTab (juce::Point<float> devicePos) const;

    void paintScreen (juce::Graphics&, juce::Rectangle<float> area) const;
    void paintControl (juce::Graphics&, const Control&) const;
    void paintRateTab (juce::Graphics&) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JXStepSequencerEditor)
};
