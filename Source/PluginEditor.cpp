#include "PluginEditor.h"

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

    const juce::Rectangle<float> screenRect { 6.0f, 4.0f, 345.0f, 96.0f };
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
    };

    display = proc.getDisplay();
    shownRate = proc.getRateParam().getIndex();

    setResizable (true, true);
    setResizeLimits (baseWidth, baseHeight, baseWidth * 3, baseHeight * 3);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (juce::roundToInt (baseWidth * proc.editorScale), juce::roundToInt (baseHeight * proc.editorScale));

    startTimerHz (30);
}

float JXStepSequencerEditor::scale() const
{
    return (float) getWidth() / (float) baseWidth;
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

void JXStepSequencerEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = toDevice (e.position);

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
    proc.pushCommand (c.cmd);
    if (c.kind == Kind::Round) c.flashUntil = juce::Time::getMillisecondCounterHiRes() + 150.0;
    repaint();
}

void JXStepSequencerEditor::mouseMove (const juce::MouseEvent& e)
{
    const int i = findControl (toDevice (e.position));
    if (i != hover)
    {
        hover = i;
        setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
}

void JXStepSequencerEditor::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

juce::String JXStepSequencerEditor::getTooltip()
{
    if (hover == rateHover) return "Length of one step.";
    if (hover >= 0 && hover < (int) controls.size()) return controls[(size_t) hover].hint;
    return {};
}

// ------------------------------------------------------------------ painting

void JXStepSequencerEditor::paint (juce::Graphics& g)
{
    g.fillAll (deviceBg);
    g.addTransform (juce::AffineTransform::scale (scale()));

    paintScreen (g, screenRect);

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
}

void JXStepSequencerEditor::paintControl (juce::Graphics& g, const Control& c) const
{
    if (c.kind == Kind::Text)            // live.text toggle
    {
        const bool on = (c.cmd == JXStepSequencerProcessor::cmdRec) ? display.rec : display.play;
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

        if (type == 'n')      fillRect (g, x + 1, cy, cw - 2, ch, rgba (0.55f, 0.3f, 0.05f));
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
}
