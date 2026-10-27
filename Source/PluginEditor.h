#pragma once

#include "PluginProcessor.h"

// The plugin window. Everything is drawn in the M4L device's own coordinates
// (357 x 169, plus a 43 px column on the right) and scaled, so the layout matches
// the presentation view.
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
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;
    void openTransposeList() { trnOpen = true; trnTop = juce::jlimit (-24 + trnRows - 1, 24, proc.getTransposeParam().get() + trnRows / 2); repaint(); }

    static constexpr int baseWidth = 400;   // 357 in the M4L device, + a column for HELP / MIDI
    static constexpr int baseHeight = 169;

    // Draws the original Max for Live layout (357 wide, no HELP/MIDI, no stripes). For product images.
    static inline bool amxdLook = false;

private:
    enum class Kind { Text, Round, Tab, Drag };

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
    int pressed = -1;                        // control under the mouse-down (for the MIDI drag)
    bool dragStarted = false;
    bool overStepLast = false;

    // transpose on the LCD: "TRN +5 v" opens a click-only list; "KEY" toggles key transpose
    int shownTrn = 0;
    bool shownKey = false;
    bool trnOpen = false;
    int trnTop = 7;                          // value shown in the first visible row of the list
    static constexpr int trnRows = 5;
    juce::Rectangle<float> trnArea() const;  // the TRN readout (device coordinates)
    juce::Rectangle<float> keyArea() const;  // the KEY word
    juce::Rectangle<float> trnPopup() const; // the open list
    void paintTransposePopup (juce::Graphics&) const;

    juce::File writeSequenceTo (const juce::File& folder) const;

    void timerCallback() override;
    float scale() const;
    juce::Point<float> toDevice (juce::Point<float> p) const;
    int findControl (juce::Point<float> devicePos) const;
    int hitTab (juce::Point<float> devicePos) const;
    int hitStep (juce::Point<float> devicePos) const;   // 1-based step under the mouse, 0 = none

    void paintScreen (juce::Graphics&, juce::Rectangle<float> area) const;
    void paintControl (juce::Graphics&, const Control&) const;
    void paintRateTab (juce::Graphics&) const;
    void paintStripes (juce::Graphics&) const;
    static juce::Rectangle<float> stripesArea() { return { 321, 110, 38, 46 }; }   // links to the Gumroad store
    bool overStripes = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JXStepSequencerEditor)
};
