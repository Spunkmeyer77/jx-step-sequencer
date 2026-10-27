#include "PluginEditor.h"
#include "MidiExport.h"

#ifndef JX_VERSION_STRING
 #define JX_VERSION_STRING JucePlugin_VersionString
#endif

#include <algorithm>
#include <cmath>

namespace
{
    juce::Colour rgba (float r, float g, float b, float a = 1.0f)
    {
        return juce::Colour::fromFloatRGBA (r, g, b, a);
    }

    juce::Font arialBold (float size)
    {
        // The device uses Arial Bold; fall back to the system sans font if Arial is missing
        static const juce::String face = juce::Font::findAllTypefaceNames().contains ("Arial")
                                             ? juce::String ("Arial")
                                             : juce::Font::getDefaultSansSerifFontName();
        return juce::Font (juce::FontOptions (face, size, juce::Font::bold)).withPointHeight (size);
    }

    enum class Align { Left, Center, Right };

    // mgraphics-style text: (x, y) is the baseline start, like move_to + show_text
    void label (juce::Graphics& g, const juce::String& str, float x, float y, float size,
                juce::Colour c, Align align = Align::Left)
    {
        const auto font = arialBold (size);
        juce::GlyphArrangement measure;
        measure.addLineOfText (font, str, 0.0f, 0.0f);
        const float w = measure.getBoundingBox (0, -1, true).getWidth();
        float px = x;
        if (align == Align::Center) px = x - w / 2.0f;
        if (align == Align::Right)  px = x - w;

        juce::GlyphArrangement ga;
        ga.addLineOfText (font, str, px, y);
        g.setColour (c);
        ga.draw (g);
    }

    void fillRect (juce::Graphics& g, float x, float y, float w, float h, juce::Colour c)
    {
        g.setColour (c);
        g.fillRect (x, y, w, h);
    }

    void strokeRect (juce::Graphics& g, float x, float y, float w, float h, juce::Colour c, float lw)
    {
        g.setColour (c);
        g.drawRect (x - lw / 2.0f, y - lw / 2.0f, w + lw, h + lw, lw);   // centred on the path, like cairo
    }

    juce::String pad (int n, int len)
    {
        return juce::String (n).paddedLeft ('0', len);
    }

    const juce::Colour deviceBg  = rgba (0.157f, 0.157f, 0.157f);
    const juce::Colour commentCol = rgba (1.0f, 1.0f, 1.0f, 0.6f);

    const juce::Rectangle<float> screenRect { 6.0f, 4.0f, 388.0f, 96.0f };

    // JX-3P panel colours (same as the Rest / Tie / Clear buttons)
    const juce::Colour jxGreen = juce::Colour (65, 141, 116);
    const juce::Colour jxBlue  = juce::Colour (74, 105, 183);
    const juce::Colour jxRed   = juce::Colour (135, 43, 41);

    // Three "/" stripes: green, blue, red. The blue one trails a thin line to the
    // right and the red one turns into a horizontal bar, like on the JX-3P panel.
    void drawStripeSet (juce::Graphics& g, juce::Rectangle<float> area, float thick, float gap,
                        float slant, float rightEdge, bool trailLines)
    {
        const juce::Colour cols[] = { jxGreen, jxBlue, jxRed };
        const float y0 = area.getY(), y1 = area.getBottom();
        for (int i = 0; i < 3; ++i)
        {
            const float x = area.getX() + (float) i * (thick + gap);
            juce::Path p;
            p.startNewSubPath (x, y1);
            p.lineTo (x + thick, y1);
            p.lineTo (x + thick + slant, y0);
            p.lineTo (x + slant, y0);
            p.closeSubPath();
            g.setColour (cols[i]);
            g.fillPath (p);

            if (trailLines && i == 1)
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x + thick + slant, y0, rightEdge, y0 + 1.2f));
            if (trailLines && i == 2)
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x + slant, y0, rightEdge, y0 + thick * 0.75f));
        }
    }
    const juce::Rectangle<float> rateRect   { 48.0f, 134.0f, 124.0f, 20.0f };
    constexpr int rateHover = 100;
}

JXStepSequencerEditor::JXStepSequencerEditor (JXStepSequencerProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    using C = JXStepSequencerProcessor;
    const auto defaultBtn = rgba (0.36f, 0.36f, 0.36f);
    const auto mint = rgba (0.45f, 0.85f, 0.68f);

    // Positions and colours are the presentation_rects / colours from the .amxd
    controls = {
        { "write", Kind::Text,  { 12, 110, 58, 17 }, C::cmdRec,
          rgba (0.24f, 0.08f, 0.07f), rgba (0.878f, 0.188f, 0.125f), rgba (0.8f, 0.4f, 0.35f), rgba (1, 1, 1), "REC",
          "Write mode. Turning it on starts a new sequence at step 1. Play chords on your keyboard: each chord becomes one step." },
        { "rest",  Kind::Round, { 78, 111, 15, 15 }, C::cmdRest,
          rgba (0.255f, 0.553f, 0.455f), mint, {}, {}, "", "While writing: add a rest step." },
        { "tie",   Kind::Round, { 140, 111, 15, 15 }, C::cmdTie,
          rgba (0.29f, 0.412f, 0.718f), rgba (0.5f, 0.66f, 1.0f), {}, {}, "",
          "While writing: tie the previous step through this one (no new attack)." },
        { "start", Kind::Text,  { 197, 110, 58, 17 }, C::cmdPlay,
          rgba (0.07f, 0.2f, 0.14f), rgba (0.31f, 0.78f, 0.6f), rgba (0.4f, 0.72f, 0.58f), rgba (0.03f, 0.12f, 0.08f), "PLAY",
          "Start / stop the sequence. It follows the host's play button and tempo." },
        { "clear", Kind::Round, { 268, 111, 15, 15 }, C::cmdClear,
          rgba (0.529f, 0.169f, 0.161f), rgba (0.878f, 0.188f, 0.125f), {}, {}, "", "Erase the whole sequence." },
        { "prev",  Kind::Round, { 204, 137, 15, 15 }, C::cmdPrev,
          defaultBtn, mint, {}, {}, "", "Select the previous step (wraps around). Not available while recording." },
        { "next",  Kind::Round, { 226, 137, 15, 15 }, C::cmdNext,
          defaultBtn, mint, {}, {}, "", "Select the next step (wraps around). Not available while recording." },
        { "erase", Kind::Round, { 268, 137, 15, 15 }, C::cmdErase,
          defaultBtn, mint, {}, {}, "",
          "Blank the selected step (it becomes a rest). While recording it removes the last step you entered." },

        // added in the VST3 version
        { "help",  Kind::Text,  { 358, 110, 34, 17 }, (C::Command) 0,
          rgba (0.22f, 0.22f, 0.22f), rgba (1.0f, 0.62f, 0.1f), rgba (0.7f, 0.7f, 0.7f), rgba (0.12f, 0.07f, 0.02f), "HELP",
          "Show or hide these help texts." },
        { "midi",  Kind::Drag,  { 358, 136, 34, 17 }, (C::Command) 0,
          rgba (0.35f, 0.22f, 0.05f), rgba (0.55f, 0.35f, 0.08f), rgba (1.0f, 0.62f, 0.1f), rgba (1.0f, 0.85f, 0.5f), "MIDI",
          "Drag this onto Live's Arrangement (or a clip slot) to drop the sequence in as a MIDI clip. Click to save it as a .mid file instead." },
    };

    if (amxdLook)
        controls.erase (std::remove_if (controls.begin(), controls.end(),
                                        [] (const Control& c) { return c.id == "help" || c.id == "midi"; }),
                        controls.end());

    display = proc.getDisplay();
    shownRate = proc.getRateParam().getIndex();

    setResizable (true, true);
    setResizeLimits (baseWidth, baseHeight, baseWidth * 3, baseHeight * 3);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (juce::roundToInt ((amxdLook ? 357 : baseWidth) * proc.editorScale), juce::roundToInt (baseHeight * proc.editorScale));

    startTimerHz (30);
}

float JXStepSequencerEditor::scale() const
{
    return (float) getHeight() / (float) baseHeight;
}

juce::Point<float> JXStepSequencerEditor::toDevice (juce::Point<float> p) const
{
    return p / scale();
}

void JXStepSequencerEditor::resized()
{
    proc.editorScale = scale();
}

void JXStepSequencerEditor::timerCallback()
{
    bool dirty = false;
    const auto d = proc.getDisplay();
    if (d != display) { display = d; dirty = true; }

    const int trn = proc.getTransposeParam().get();
    const bool key = proc.getKeyParam().get();
    if (trn != shownTrn || key != shownKey) { shownTrn = trn; shownKey = key; dirty = true; }

    const int r = proc.getRateParam().getIndex();
    if (r != shownRate) { shownRate = r; dirty = true; }

    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto& c : controls)
        if (c.flashUntil > 0.0 && now >= c.flashUntil) { c.flashUntil = 0.0; dirty = true; }

    if (dirty) repaint();
}

// ------------------------------------------------------------------ mouse

int JXStepSequencerEditor::findControl (juce::Point<float> p) const
{
    for (int i = 0; i < (int) controls.size(); ++i)
        if (controls[(size_t) i].r.expanded (2.0f).contains (p)) return i;
    if (rateRect.contains (p)) return rateHover;
    return -1;
}

int JXStepSequencerEditor::hitTab (juce::Point<float> p) const
{
    if (! rateRect.contains (p)) return -1;
    return juce::jlimit (0, 3, (int) ((p.x - rateRect.getX()) / (rateRect.getWidth() / 4.0f)));
}

int JXStepSequencerEditor::hitStep (juce::Point<float> p) const
{
    const auto area = amxdLook ? screenRect.withWidth (345.0f) : screenRect;
    const float w = area.getWidth(), h = area.getHeight();
    const float cw = (w - 16) / 16, cy = area.getY() + 48;
    const float ch = juce::jmin (34.0f, h - 48 - 20);
    if (p.y < cy || p.y > cy + ch + 16) return 0;            // the cell or its number
    const int col = (int) std::floor ((p.x - area.getX() - 8) / cw);
    if (col < 0 || col > 15) return 0;

    const auto& d = display;                                 // same page as the screen shows
    int active = 1;
    if (d.play && d.cur > 0) active = d.cur;
    else if (d.rec) active = d.wr;
    else if (d.sel > 0) active = d.sel;
    else if (d.cur > 0) active = d.cur;
    const int idx = juce::jmax (0, (active - 1) / 16) * 16 + col + 1;
    return (idx <= d.count && ! d.rec) ? idx : 0;
}

juce::Rectangle<float> JXStepSequencerEditor::trnArea() const { return { 6 + 176, 4 + 20, 66, 18 }; }

juce::Rectangle<float> JXStepSequencerEditor::keyArea() const
{
    const auto area = amxdLook ? screenRect.withWidth (345.0f) : screenRect;
    juce::GlyphArrangement ga;
    ga.addLineOfText (arialBold (11), "PAGE 8/8", 0, 0);
    const float pageLeft = area.getX() + area.getWidth() - 92 - ga.getBoundingBox (0, -1, true).getWidth();
    return { pageLeft - 2, 4 + 22, 30, 16 };
}

juce::Rectangle<float> JXStepSequencerEditor::trnPopup() const { return { 196, 38, 52, 14 + trnRows * 10.5f + 12 }; }

void JXStepSequencerEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = toDevice (e.position);

    if (trnOpen)                                         // the list takes the click
    {
        const auto box = trnPopup();
        if (box.contains (p))
        {
            if (p.y < box.getY() + 10)                   trnTop = juce::jmin (24, trnTop + trnRows);          // up arrow: higher values
            else if (p.y > box.getBottom() - 12)         trnTop = juce::jmax (-24 + trnRows - 1, trnTop - trnRows);
            else
            {
                const int row = juce::jlimit (0, trnRows - 1, (int) ((p.y - box.getY() - 11) / 10.5f));
                const int v = trnTop - row;
                if (v >= -24 && v <= 24)
                {
                    auto& prm = proc.getTransposeParam();
                    prm.beginChangeGesture(); prm = v; prm.endChangeGesture();
                    trnOpen = false;
                }
            }
        }
        else trnOpen = false;                            // click outside closes, nothing changes
        repaint();
        return;
    }

    if (! amxdLook && trnArea().contains (p))
    {
        trnOpen = true;
        trnTop = juce::jlimit (-24 + trnRows - 1, 24, proc.getTransposeParam().get() + trnRows / 2);
        repaint();
        return;
    }
    if (! amxdLook && keyArea().contains (p))
    {
        auto& kp = proc.getKeyParam();
        kp.beginChangeGesture(); kp = ! kp.get(); kp.endChangeGesture();
        repaint();
        return;
    }

    if (const int step = hitStep (p); step > 0)
    {
        proc.selectStep (step);
        return;
    }

    const int tab = hitTab (p);
    if (tab >= 0)
    {
        auto& param = proc.getRateParam();
        param.beginChangeGesture();
        param = tab;
        param.endChangeGesture();
        shownRate = tab;
        repaint();
        return;
    }

    const int i = findControl (p);
    if (i < 0 || i == rateHover) return;
    auto& c = controls[(size_t) i];
    if (c.id == "help")
    {
        proc.showHelp = ! proc.showHelp.load();
        if (! proc.showHelp) tooltipWindow.hideTip();
        repaint();
        return;
    }
    if (c.id == "midi")
    {
        pressed = i;
        dragStarted = false;
        repaint();
        return;
    }
    proc.pushCommand (c.cmd);
    if (c.kind == Kind::Round) c.flashUntil = juce::Time::getMillisecondCounterHiRes() + 150.0;
    repaint();
}

void JXStepSequencerEditor::mouseMove (const juce::MouseEvent& e)
{
    const int i = findControl (toDevice (e.position));
    const bool overStep = hitStep (toDevice (e.position)) > 0;
    if (overStep != overStepLast) { overStepLast = overStep; setMouseCursor (overStep ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
    if (i != hover)
    {
        hover = i;
        const bool overMidi = i >= 0 && i < (int) controls.size() && controls[(size_t) i].id == "midi";
        setMouseCursor (overMidi ? juce::MouseCursor::DraggingHandCursor
                        : i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void JXStepSequencerEditor::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

juce::File JXStepSequencerEditor::writeSequenceTo (const juce::File& folder) const
{
    auto steps = proc.getSequence();
    const int tr = proc.getTransposeParam().get();          // export what you hear
    for (auto& st : steps)
        for (int n = 0; n < st.numNotes; ++n)
            st.pitch[(size_t) n] = (juce::uint8) juce::jlimit (0, 127, (int) st.pitch[(size_t) n] + tr);
    if (steps.empty()) return {};
    folder.createDirectory();
    const auto f = folder.getChildFile ("JX Sequence " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".mid");
    return jx::writeMidiFile (f, steps, proc.getQuartersPerStep()) ? f : juce::File();
}

void JXStepSequencerEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0 || controls[(size_t) pressed].id != "midi" || dragStarted) return;
    if (e.getDistanceFromDragStart() < 4) return;
    dragStarted = true;

    const auto f = writeSequenceTo (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                        .getChildFile ("JX Step Sequencer"));
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void JXStepSequencerEditor::mouseUp (const juce::MouseEvent& e)
{
    if (pressed >= 0 && controls[(size_t) pressed].id == "midi" && ! dragStarted
        && controls[(size_t) pressed].r.contains (toDevice (e.position)))
    {
        const auto f = writeSequenceTo (juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                            .getChildFile ("JX Step Sequencer"));
        if (f.existsAsFile()) f.revealToUser();
    }
    pressed = -1;
    dragStarted = false;
    repaint();
}

juce::String JXStepSequencerEditor::getTooltip()
{
    if (! proc.showHelp) return {};
    if (hover == rateHover) return "Length of one step.";
    if (hover >= 0 && hover < (int) controls.size()) return controls[(size_t) hover].hint;
    return {};
}

// ------------------------------------------------------------------ painting

void JXStepSequencerEditor::paint (juce::Graphics& g)
{
    g.fillAll (deviceBg);
    g.addTransform (juce::AffineTransform::scale (scale()));

    paintScreen (g, amxdLook ? screenRect.withWidth (345.0f) : screenRect);
    if (! amxdLook) paintStripes (g);

    // maker + version, quiet, bottom-left
    if (! amxdLook)
        label (g, "v" JX_VERSION_STRING, 12, 165, 7.5f, rgba (1, 1, 1, 0.32f));

    for (const auto& c : controls) paintControl (g, c);
    paintRateTab (g);

    // comments (the device's text labels)
    label (g, "Rest",  98 + 3, 109 + 13, 10, commentCol);
    label (g, "Tie",   160 + 3, 109 + 13, 10, commentCol);
    label (g, "Clear", 288 + 3, 109 + 13, 10, commentCol);
    label (g, "Rate",  12 + 3, 133 + 13, 10, commentCol);
    label (g, "<",     190 + 3, 134 + 13, 10, commentCol);
    label (g, ">",     246 + 3, 134 + 13, 10, commentCol);
    label (g, "Erase", 288 + 3, 134 + 13, 10, commentCol);

    if (trnOpen) paintTransposePopup (g);   // on top of everything
}

void JXStepSequencerEditor::paintStripes (juce::Graphics& g) const
{
    // JX-3P stripes, small badge between Clear/Erase and HELP/MIDI
    drawStripeSet (g, { 323, 112, 34, 40 }, 5.0f, 3.0f, 12.0f, 356.0f, false);
}

void JXStepSequencerEditor::paintTransposePopup (juce::Graphics& g) const
{
    const auto RED  = rgba (1.0f, 0.25f, 0.2f);
    const auto SOFT = rgba (0.59f, 0.19f, 0.16f);
    const auto DIM  = rgba (0.35f, 0.12f, 0.1f);
    const auto box = trnPopup();
    const int cur = proc.getTransposeParam().get();

    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 8, { 2, 4 }).drawForRectangle (g, box.toNearestInt());
    g.setColour (rgba (0.086f, 0.047f, 0.043f));
    g.fillRoundedRectangle (box, 5);
    g.setColour (rgba (0.47f, 0.18f, 0.15f));
    g.drawRoundedRectangle (box.reduced (0.5f), 5, 1);

    const float cx = box.getCentreX();
    juce::Path up, down;
    up.addTriangle (cx - 4, box.getY() + 8, cx + 4, box.getY() + 8, cx, box.getY() + 4);
    down.addTriangle (cx - 4, box.getBottom() - 8, cx + 4, box.getBottom() - 8, cx, box.getBottom() - 4);
    g.setColour (trnTop < 24 ? SOFT : DIM);              g.fillPath (up);
    g.setColour (trnTop - trnRows + 1 > -24 ? SOFT : DIM); g.fillPath (down);

    for (int i = 0; i < trnRows; ++i)
    {
        const int v = trnTop - i;
        if (v < -24) break;
        const float y = box.getY() + 11 + i * 10.5f;
        if (v == cur)
        {
            g.setColour (rgba (0.235f, 0.063f, 0.055f));
            g.fillRoundedRectangle ({ box.getX() + 4, y - 0.5f, box.getWidth() - 8, 10.5f }, 3);
        }
        const juce::String t = v > 0 ? "+" + juce::String (v) : juce::String (v);
        label (g, t, cx, y + 8.5f, 9, v == cur ? RED : SOFT, Align::Center);
    }
}

void JXStepSequencerEditor::paintControl (juce::Graphics& g, const Control& c) const
{
    if (c.kind == Kind::Drag)            // MIDI drag-out handle
    {
        const bool empty = display.count == 0;
        const bool lit = ! empty && (hover >= 0 && &controls[(size_t) hover] == &c);
        g.setColour ((lit ? c.bgOn : c.bg).withMultipliedAlpha (empty ? 0.4f : 1.0f));
        g.fillRoundedRectangle (c.r, 6.0f);
        g.setColour (rgba (0, 0, 0, 0.35f));
        g.drawRoundedRectangle (c.r.reduced (0.5f), 6.0f, 1.0f);
        label (g, c.label, c.r.getCentreX(), c.r.getY() + 12.0f, 10,
               (lit ? c.textOn : c.text).withMultipliedAlpha (empty ? 0.4f : 1.0f), Align::Center);
        return;
    }

    if (c.kind == Kind::Text)            // live.text toggle
    {
        const bool on = c.id == "help" ? proc.showHelp.load()
                      : (c.cmd == JXStepSequencerProcessor::cmdRec) ? display.rec : display.play;
        g.setColour (on ? c.bgOn : c.bg);
        g.fillRoundedRectangle (c.r, 6.0f);
        g.setColour (rgba (0, 0, 0, 0.35f));
        g.drawRoundedRectangle (c.r.reduced (0.5f), 6.0f, 1.0f);
        label (g, c.label, c.r.getCentreX(), c.r.getY() + 12.0f, 10, on ? c.textOn : c.text, Align::Center);
    }
    else                                 // live.button
    {
        const bool lit = c.flashUntil > 0.0;
        const auto r = c.r.reduced (1.0f);
        g.setColour (lit ? c.bgOn : c.bg);
        g.fillEllipse (r);
        g.setColour (rgba (0, 0, 0, 0.35f));
        g.drawEllipse (r, 1.0f);
    }
}

void JXStepSequencerEditor::paintRateTab (juce::Graphics& g) const
{
    static const char* names[] = { "1/4", "1/8", "1/16", "1/32" };
    const float segW = rateRect.getWidth() / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        const auto seg = juce::Rectangle<float> (rateRect.getX() + i * segW, rateRect.getY(), segW, rateRect.getHeight())
                             .reduced (1.0f, 2.0f);
        const bool on = (i == shownRate);
        g.setColour (on ? rgba (1.0f, 0.71f, 0.2f) : rgba (0.27f, 0.27f, 0.27f));
        g.fillRect (seg);
        g.setColour (rgba (0, 0, 0, 0.35f));
        g.drawRect (seg, 1.0f);
        label (g, names[i], seg.getCentreX(), seg.getY() + 11.5f, 9.5f,
               on ? rgba (0.07f, 0.07f, 0.07f) : rgba (0.82f, 0.82f, 0.82f), Align::Center);
    }
}

// Port of jxscreen.js paint()
void JXStepSequencerEditor::paintScreen (juce::Graphics& g, juce::Rectangle<float> area) const
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.toNearestInt());
    g.addTransform (juce::AffineTransform::translation (area.getX(), area.getY()));

    const float w = area.getWidth();
    const float h = area.getHeight();

    const auto BG    = rgba (0.07f, 0.04f, 0.04f);
    const auto RED   = rgba (1.0f, 0.25f, 0.2f);
    const auto DIM   = rgba (0.35f, 0.12f, 0.1f);
    const auto AMBER = rgba (1.0f, 0.62f, 0.1f);
    const auto BLUE  = rgba (0.3f, 0.6f, 1.0f);
    const auto GREY  = rgba (0.45f, 0.45f, 0.45f);
    const auto WHITE = rgba (1, 1, 1);

    const auto& d = display;

    fillRect (g, 0, 0, w, h, BG);
    strokeRect (g, 0.5f, 0.5f, w - 1, h - 1, rgba (0.3f, 0.3f, 0.3f), 1);

    // which step do we show on the big display
    int active = 1;
    if (d.play && d.cur > 0) active = d.cur;
    else if (d.rec) active = d.wr;
    else if (d.sel > 0) active = d.sel;
    else if (d.cur > 0) active = d.cur;

    // ---- LCD row ----
    label (g, "STEP", 12, 26, 11, DIM);
    label (g, pad (active, 3), 52, 34, 30, RED);
    label (g, "/ " + pad (d.count, 3), 128, 34, 14, DIM);

    // status LEDs
    label (g, "REC",  w - 12, 18, 11, d.rec ? RED : DIM, Align::Right);
    label (g, "PLAY", w - 52, 18, 11, d.play ? rgba (0.2f, 1.0f, 0.4f) : DIM, Align::Right);

    // notes of the current step
    label (g, jx::infoText (d.info), w - 12, 38, 14, AMBER, Align::Right);

    // ---- step cells (one page of 16) ----
    const int page = juce::jmax (0, (active - 1) / 16);
    const float cx = 8, cy = 48;
    const float cw = (w - 16) / 16;
    float ch = h - cy - 20;
    if (ch > 34) ch = 34;

    for (int i = 0; i < 16; ++i)
    {
        const int idx = page * 16 + i + 1;
        const float x = cx + (float) i * cw;
        const char type = (idx <= d.count) ? d.cells[(size_t) idx - 1] : 0;

        const bool isChord = idx <= d.count && d.chord[(size_t) idx - 1];
        if (type == 'n' && isChord) fillRect (g, x + 1, cy, cw - 2, ch, rgba (0.96f, 0.84f, 0.32f));   // chord: light yellow
        else if (type == 'n') fillRect (g, x + 1, cy, cw - 2, ch, rgba (0.55f, 0.3f, 0.05f));
        else if (type == 't') fillRect (g, x + 1, cy, cw - 2, ch, rgba (0.1f, 0.25f, 0.45f));
        else if (type == 'r') fillRect (g, x + 1, cy, cw - 2, ch, rgba (0.18f, 0.18f, 0.18f));
        else                  fillRect (g, x + 1, cy, cw - 2, ch, rgba (0.12f, 0.08f, 0.08f));

        // type marker
        if (type == 'r') label (g, "-", x + cw / 2, cy + ch / 2 + 4, 12, GREY, Align::Center);
        if (type == 't') label (g, "~", x + cw / 2, cy + ch / 2 + 4, 12, BLUE, Align::Center);

        // playhead / write cursor
        if (d.play && idx == d.cur)
        {
            fillRect (g, x + 1, cy, cw - 2, ch, rgba (1, 1, 1, 0.35f));
            strokeRect (g, x + 1.5f, cy + 0.5f, cw - 3, ch - 1, WHITE, 2);
        }
        if (d.rec && idx == d.wr)
            strokeRect (g, x + 1.5f, cy + 0.5f, cw - 3, ch - 1, RED, 2);

        // selected step (navigation / erase target)
        if (d.sel > 0 && idx == d.sel && ! d.rec)
            strokeRect (g, x + 0.5f, cy - 1, cw - 1, ch + 2, AMBER, 2);

        // step number under the cell
        auto numCol = DIM;
        if (d.play && idx == d.cur) numCol = WHITE;
        else if (d.rec && idx == d.wr) numCol = RED;
        else if (d.sel > 0 && idx == d.sel && ! d.rec) numCol = AMBER;
        else if (type != 0) numCol = AMBER;
        label (g, juce::String (idx), x + cw / 2, cy + ch + 13, 9, numCol, Align::Center);
    }

    // page indicator
    label (g, "PAGE " + juce::String (page + 1) + "/8", w - 92, 18, 11, DIM, Align::Right);

    // transpose readout and KEY (VST3 / AU only)
    if (! amxdLook)
    {
        const int trn = proc.getTransposeParam().get();
        const juce::String tv = trn > 0 ? "+" + juce::String (trn) : juce::String (trn);
        label (g, "TRN", 178, 34, 9, DIM);
        label (g, tv, 200, 34, 14, DIM);
        juce::Path tri;
        tri.addTriangle (226, 28, 232, 28, 229, 31.5f);
        g.setColour (DIM); g.fillPath (tri);

        juce::GlyphArrangement ga;
        ga.addLineOfText (arialBold (11), "PAGE 8/8", 0, 0);
        const float pageLeft = w - 92 - ga.getBoundingBox (0, -1, true).getWidth();
        label (g, "KEY", pageLeft, 34, 9, proc.getKeyParam().get() ? RED : DIM);
    }
}
