#include <cstdlib>
#include "PluginEditor.h"

#include <cmath>
#include <iterator>

namespace tympanis::ui
{

namespace
{
// Font sizes and letter spacing (Cymbalis's).
constexpr float kHeaderSize = 17.1f;
constexpr float kRowSize = 15.6f;
constexpr float kBoxValueSpacing = 3.2f;
constexpr float kBoxValueLeft = 12.0f;
constexpr float kNoteValueSpacing = 1.0f;
constexpr float kPopupSpacing = 2.5f;

// The pages' common parts (as the approved mockups).
const juce::Rectangle<int> kDrumBox (61, 371, 277, 62);
// The TUNING page's left column (Kris, 2026-11-27): the DRUM box (with its label), the spec line and
// the small drum spaced evenly between the tabs (their line at y 234) and the logo (ink from y 935),
// about 97 apart; the DRUM page keeps the column as it was.
constexpr int kTuneDrumDy = -17;         // the label and the box, up from the DRUM page's
constexpr float kTuneSpecY = 512.0f;     // the spec line (452 on the DRUM page)
const juce::Rectangle<int> kChannelBox (1404, 30, 110, 36);
constexpr float kChannelLabelRight = 1384.0f;
constexpr float kTabTop = 208.0f,   // 258 until 2026-11-27 (Kris: the header higher, a little tighter)
                kTabLeft = 36.0f, kTabGap = 30.0f, kTabSpacing = 3.3f;
constexpr float kLogoSize = 43.2f;                  // 10 % under the mockup's 48 (Kris, 2026-11-27)
constexpr float kLogoLeft = 36.0f, kLogoMidY = 950.0f; // left-aligned with the title and the tabs

// DRUM page: the note rows' tops, the note boxes.
constexpr std::array<int, 3> kRowTop { 140, 220, 300 };
const juce::Rectangle<int> kNoteBox (1424, 0, 66, 25);
constexpr float kKnobLabelAbove = 97.0f; // a knob's label's ink top, above its centre
constexpr float kKnobEndBelow = 77.0f;   // its end labels' ink top, below its centre
constexpr float kKnobEndSide = 73.0f;    // and their centres, each side

// TUNING page: the rows' tops, the slider, the number box, the reset arrow.
// The drum's rows are 56 apart (9 on the snares; Kris, 2026-11-26, layout A), the WHOLE KIT rows 62.
constexpr int kTuneFirstRow = 160, kTuneRowStep = 62, kDrumRowStep = 56;
constexpr int kKitBandTop = 682, kKitFirstRow = 742, kButtonsY = 892;
constexpr float kSliderX0 = 640.0f, kSliderX1 = 1236.0f;
constexpr float kThumbRadius = 7.0f;
const juce::Rectangle<int> kNumberBox (1256, -3, 104, 24); // y relative to the row's top
constexpr float kResetX = 1384.0f;

const juce::String kEmDash = juce::String::fromUTF8 ("\xe2\x80\x94");
const juce::String kDot = juce::String::fromUTF8 ("\xc2\xb7");
const juce::String kTimes = juce::String::fromUTF8 ("\xc3\x97");

float capBaseline (const juce::Font& f, juce::Rectangle<float> area)
{
    return area.getCentreY() + inkBounds (f, "H").getHeight() * 0.5f;
}

void drawValue (juce::Graphics& g, const juce::Font& f, const juce::String& text, juce::Rectangle<float> area, juce::Colour c)
{
    const auto b = inkBounds (f, text);
    drawInkText (g, f, text, { area.getCentreX() - b.getWidth() * 0.5f, capBaseline (f, area) + b.getY() }, c);
}

void drawValueLeft (juce::Graphics& g, const juce::Font& f, const juce::String& text, juce::Rectangle<float> area, juce::Colour c)
{
    drawInkText (g, f, text, { area.getX(), capBaseline (f, area) + inkBounds (f, text).getY() }, c);
}

// `text` with its ink's top at y: left from x, centred on x, or ending at x.
enum class Anchor { left, centre, right };
void drawAt (juce::Graphics& g, const juce::Font& f, const juce::String& text, float x, float y, juce::Colour c, Anchor anchor = Anchor::left)
{
    if (text.isEmpty())
        return;
    const auto b = inkBounds (f, text);
    const float left = anchor == Anchor::left ? x : (anchor == Anchor::centre ? x - b.getWidth() * 0.5f : x - b.getWidth());
    drawInkText (g, f, text, { left, y }, c);
}

void drawTriangle (juce::Graphics& g, juce::Point<float> topLeft)
{
    juce::Path p;
    p.addTriangle (topLeft, topLeft.translated (13.0f, 0.0f), topLeft.translated (6.5f, 9.0f));
    g.setColour (palette::arrow);
    g.fillPath (p);
}

void drawBoxOutline (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c, int thickness = 2)
{
    g.setColour (c);
    g.drawRect (r, thickness);
}

juce::String sizeText (double inches)
{
    return juce::String (inches, std::abs (inches - std::round (inches)) < 1e-6 ? 0 : 1) + "\"";
}

juce::String drumTitle (int d)
{
    return juce::String (kDrums[(size_t) d].name).toUpperCase() + "  " + kDot + "  " + sizeText (wire::drumLook (d).diameter);
}

// depthIn: the shell's depth played (Size on Toms 1-4), < 0: as drawn by default.
juce::String drumSpecLine (int d, double depthIn = -1)
{
    auto look = wire::drumLook (d);
    if (depthIn > 0)
        look.depth = depthIn;
    const juce::String lugs = "  " + juce::String (kDot) + "  " + juce::String (look.lugs) + " LUGS";
    if (look.shellDiameter > 0) // the gong drum: its head is larger than its shell
        return sizeText (look.diameter) + " HEAD ON A " + sizeText (look.shellDiameter) + " " + kTimes + " " + sizeText (look.depth) + " SHELL" + lugs;
    return sizeText (look.diameter) + " " + kTimes + " " + sizeText (look.depth) + lugs;
}

const char* artLabel (Art a)
{
    switch (a)
    {
        case Art::Head: return "HEAD";
        case Art::Rim: return "RIM SHOT";
        case Art::Side: return "SIDE STICK";
    }
    return "";
}

juce::String levelText (float v)
{
    return v <= TympanisProcessor::kLevelMinDb + 1e-3f ? juce::String ("-inf") : juce::String (v, 1) + " dB";
}
} // namespace

// =====================================================================================
// A knob drawn by the plugin, with its position arc (Cymbalis's Knob since 2026-11-24, its
// UI_SPEC section 6, "The knob"): vertical drag, double-click resets, no mouse wheel. Drawn in
// design units under the editor's transform, so it is sharp at every size and on Retina screens.
class Knob final : public juce::Slider
{
public:
    explicit Knob (bool isBipolar) : bipolar (isBipolar)
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setMouseDragSensitivity (300);
        setScrollWheelEnabled (false);
    }
    static constexpr int margin = 4;
    static constexpr int size = 128 + 2 * margin;

    bool hitTest (int x, int y) override { return getLocalBounds().reduced (margin).contains (x, y); }

    void paint (juce::Graphics& g) override
    {
        // p: the knob's position 0-1, which turns the pointer and lights the arc
        const float p = (float) juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()));
        const auto c = getLocalBounds().toFloat().getCentre();
        paintKnob (g, c, p);
        paintArc (g, c, -kSweep, kSweep, [] (float) { return palette::arcTrack; });
        const auto gold = [] (float t) { return palette::arcDark.interpolatedWith (palette::arcBright, juce::jlimit (0.0f, 1.0f, t)); };
        if (bipolar)
            paintArc (g, c, 0.0f, 2.0f * kSweep * (p - 0.5f), [&gold] (float a) { return gold (std::abs (a) / kSweep); });
        else
            paintArc (g, c, -kSweep, -kSweep + 2.0f * kSweep * p, [&gold] (float a) { return gold ((a + kSweep) / (2.0f * kSweep)); });
    }

private:
    static constexpr float kRadius = 63.0f, kWidth = 2.5f, kSweep = 150.0f;

    // The knob (Cymbalis's paintKnob), around the component's centre c, lit from the top left.
    // The light stays put as the knob turns: the body, the rim and the pointer's shadow offset
    // do not rotate; only the pointer does, at a = -150 + 300 p degrees clockwise from 12 o'clock.
    static constexpr float kKnobRadius = 50.0f, kKnobRimRadius = 49.9f, kKnobRimWidth = 1.8f;
    static constexpr float kKnobLight = 35.36f; // the gradients run from c + (-35.36, -35.36) to c + (35.36, 35.36)
    static constexpr float kKnobRimStops[6] = { 0.0f, 0.06f, 0.14f, 0.5f, 0.8f, 1.0f };
    static constexpr float kPointerFrom = 28.0f, kPointerTo = 45.0f, kPointerWidth = 2.0f;
    static constexpr float kPointerShadowOffset = 1.3f, kPointerShadowWidth = 1.6f;

    static void strokeSegment (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, float width, juce::Colour colour)
    {
        juce::Path segment;
        segment.startNewSubPath (from);
        segment.lineTo (to);
        g.setColour (colour);
        g.strokePath (segment, juce::PathStrokeType (width, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    }

    static void paintKnob (juce::Graphics& g, juce::Point<float> c, float p)
    {
        const juce::Point<float> light (c.x - kKnobLight, c.y - kKnobLight), dark (c.x + kKnobLight, c.y + kKnobLight);
        g.setGradientFill (juce::ColourGradient (palette::knobBodyLight, light, palette::knobBodyDark, dark, false));
        g.fillEllipse (c.x - kKnobRadius, c.y - kKnobRadius, 2.0f * kKnobRadius, 2.0f * kKnobRadius);

        constexpr size_t last = std::size (kKnobRimStops) - 1;
        static_assert (std::size (kKnobRimStops) == std::size (palette::knobRim));
        static_assert (wire::kDrumCount == (int) kDrums.size(), "plugin/ui/DrumModel.h kDrumCount must match KitMap.h");
        juce::ColourGradient rim (palette::knobRim[0], light, palette::knobRim[last], dark, false);
        for (size_t i = 1; i < last; ++i)
            rim.addColour (kKnobRimStops[i], palette::knobRim[i]);
        g.setGradientFill (rim);
        g.drawEllipse (c.x - kKnobRimRadius, c.y - kKnobRimRadius, 2.0f * kKnobRimRadius, 2.0f * kKnobRimRadius, kKnobRimWidth);

        const float a = -kSweep + 2.0f * kSweep * p;
        const auto from = onCircle (c, kPointerFrom, a), to = onCircle (c, kPointerTo, a);
        const juce::Point<float> shadow (kPointerShadowOffset, kPointerShadowOffset);
        strokeSegment (g, from + shadow, to + shadow, kPointerShadowWidth, palette::knobPointerShadow);
        strokeSegment (g, from, to, kPointerWidth, palette::knobPointer);
    }

    static juce::Point<float> onCircle (juce::Point<float> c, float radius, float degrees)
    {
        const float a = juce::degreesToRadians (degrees);
        return { c.x + radius * std::sin (a), c.y - radius * std::cos (a) };
    }

    // The arc from `from` to `to` (degrees clockwise from 12 o'clock), its colour changing
    // along it a degree at a time, clipped to the stroked arc (Cymbalis's paintArc).
    template <typename ColourAt>
    static void paintArc (juce::Graphics& g, juce::Point<float> c, float from, float to, ColourAt colourAt)
    {
        const float lo = std::min (from, to), hi = std::max (from, to);
        if (hi - lo < 1.0e-3f)
            return;
        juce::Path arc, line;
        arc.addCentredArc (c.x, c.y, kRadius, kRadius, 0.0f, juce::degreesToRadians (lo), juce::degreesToRadians (hi), true);
        juce::PathStrokeType (kWidth, juce::PathStrokeType::curved, juce::PathStrokeType::butt).createStrokedPath (line, arc);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (line);
        const int steps = std::max (1, (int) std::ceil (hi - lo));
        const float step = (hi - lo) / (float) steps;
        const float reach = kRadius + kWidth;
        for (int i = 0; i < steps; ++i)
        {
            const float a0 = i == 0 ? lo - 1.0f : lo + step * (float) i;
            const float a1 = i == steps - 1 ? hi + 1.0f : lo + step * ((float) i + 1.5f);
            juce::Path wedge;
            wedge.startNewSubPath (c);
            wedge.lineTo (onCircle (c, reach, a0));
            wedge.lineTo (onCircle (c, reach, a1));
            wedge.closeSubPath();
            g.setColour (colourAt (lo + step * ((float) i + 0.5f)));
            g.fillPath (wedge);
        }
    }

    const bool bipolar;
};

// =====================================================================================
// The DRUM and MIDI CHANNEL boxes (Cymbalis's DropdownBox).
class DropdownBox final : public juce::Component
{
public:
    enum class Align { centre, left };
    DropdownBox (const Assets& a, juce::Point<float> arrowTopLeft, Align valueAlign) : assets (a), arrow (arrowTopLeft), align (valueAlign)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void setText (const juce::String& t)
    {
        text = t;
        setTitle (t);
        repaint();
    }
    void setOpen (bool o)
    {
        open = o;
        repaint();
    }
    // the DRUM page's MIDI CHANNEL box: a drum on its own channel, outlined in bronze
    void setAccent (bool a)
    {
        accent = a;
        repaint();
    }
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        drawBoxOutline (g, getLocalBounds(), open ? palette::openOutline : (isMouseOver() || accent ? palette::bronze : palette::boxOutline), accent ? 2 : 1);
        drawTriangle (g, arrow);
        auto area = getLocalBounds().toFloat();
        auto font = assets.font (kHeaderSize, kBoxValueSpacing);
        if (align == Align::left)
        {
            drawValueLeft (g, font, text, area.withTrimmedLeft (kBoxValueLeft), palette::label);
            return;
        }
        const float right = arrow.x - 10.0f;
        if (area.getCentreX() + inkBounds (font, text).getWidth() * 0.5f > right)
        {
            area = area.withLeft (kBoxValueLeft).withRight (right);
            for (float sp = kBoxValueSpacing - 0.25f; sp >= 0.5f && inkBounds (font, text).getWidth() > area.getWidth(); sp -= 0.25f)
                font = assets.font (kHeaderSize, sp);
        }
        drawValue (g, font, text, area, palette::label);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick)
            onClick();
    }

private:
    bool accent = false;
    const Assets& assets;
    juce::Point<float> arrow;
    Align align;
    juce::String text;
    bool open = false;
};

// =====================================================================================
// A row's MIDI NOTE box (Cymbalis's NoteBox): drag to change the note, click for the menu.
class NoteBox final : public juce::Component
{
public:
    explicit NoteBox (const Assets& a) : assets (a) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }

    void setNote (int n)
    {
        note = n;
        repaint();
    }
    void setLearning (bool l, float p)
    {
        if (l != learning || (l && std::abs (p - pulse) > 0.01f))
        {
            learning = l;
            pulse = p;
            repaint();
        }
    }
    void setOpen (bool o)
    {
        open = o;
        repaint();
    }
    void setLit (bool l)
    {
        if (l != lit)
        {
            lit = l;
            repaint();
        }
    }
    std::function<void (int)> onNoteDragged;
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        drawBoxOutline (g, getLocalBounds(), open ? palette::openOutline : (isMouseOver() || lit ? palette::bronze : palette::rowOutline));
        const auto area = getLocalBounds().toFloat();
        const auto f = assets.font (kRowSize, kNoteValueSpacing);
        if (learning)
        {
            drawValue (g, f, "LEARN", area, palette::label.withMultipliedAlpha (pulse));
            return;
        }
        if (note < 0)
        {
            drawValue (g, f, kEmDash, area, palette::secondary);
            return;
        }
        const juce::String name = noteName (note), number (note);
        const auto bn = inkBounds (f, name), bm = inkBounds (f, number);
        const float gap = 6.0f;
        const float x0 = area.getCentreX() - (bn.getWidth() + gap + bm.getWidth()) * 0.5f;
        const float base = capBaseline (f, area);
        drawInkText (g, f, name, { x0, base + bn.getY() }, palette::label);
        drawInkText (g, f, number, { x0 + bn.getWidth() + gap, base + bm.getY() }, palette::secondary);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        dragStartNote = note;
        dragged = false;
    }
    // one semitone per 8 design units, up is higher; below 0 is none
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int dy = e.getDistanceFromDragStartY();
        if (! dragged && std::abs (dy) < 4)
            return;
        dragged = true;
        const int start = dragStartNote < 0 ? 60 : dragStartNote;
        const int n = juce::jlimit (-1, 127, start - dy / 8);
        if (n != note && onNoteDragged)
            onNoteDragged (n);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragged && onClick)
            onClick();
    }

private:
    const Assets& assets;
    int note = -1;
    bool learning = false, open = false, lit = false, dragged = false;
    float pulse = 1.0f;
    int dragStartNote = -1;
};

// =====================================================================================
// The note menu: MIDI LEARN, RESET DEFAULT, NONE, then every note (Cymbalis's NoteMenu).
class NoteMenu final : public juce::Component
{
public:
    enum Action { learn = 0, resetDefault = 1, none = 2 };
    static constexpr int width = 170, inset = 8, actionHeight = 34, gap = 8, rowHeight = 26, visibleRows = 10, rowsAboveCurrent = 4;
    static int height() { return 2 * inset + 3 * actionHeight + gap + visibleRows * rowHeight; }

    NoteMenu (const Assets& a, int currentNote) : assets (a), list (a, currentNote)
    {
        list.onNote = [this] (int n) {
            if (onNote)
                onNote (n);
        };
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        viewport.setSingleStepSizes (rowHeight, rowHeight);
        viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, palette::bronze);
        viewport.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, palette::popup);
        viewport.setViewedComponent (&list, false);
        addAndMakeVisible (viewport);
        setSize (width, height());
        const int first = juce::jlimit (0, 128 - visibleRows, (currentNote < 0 ? 60 : currentNote) - rowsAboveCurrent);
        viewport.setViewPosition (0, first * rowHeight);
    }
    std::function<void (int)> onAction, onNote;

    void resized() override
    {
        viewport.setBounds (inset, inset + 3 * actionHeight + gap, width - 2 * inset, visibleRows * rowHeight);
        list.setSize (viewport.getMaximumVisibleWidth(), 128 * rowHeight);
    }
    void paint (juce::Graphics& g) override
    {
        static const char* const names[] = { "MIDI LEARN", "RESET DEFAULT", "NONE" };
        g.fillAll (palette::popup);
        drawBoxOutline (g, getLocalBounds(), palette::boxOutline);
        const auto f = assets.font (kRowSize, kPopupSpacing);
        for (int i = 0; i < 3; ++i)
        {
            const auto r = actionBounds (i);
            if (i == hover)
            {
                g.setColour (palette::bronze);
                g.drawRect (r.reduced (2), 1);
            }
            drawValue (g, f, names[i], r.toFloat(), palette::label);
        }
        g.setColour (palette::divider);
        g.fillRect (inset, inset + 3 * actionHeight + gap / 2, width - 2 * inset, 1);
    }
    void mouseMove (const juce::MouseEvent& e) override { setHover (actionAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = actionAt (e.getPosition());
        if (i >= 0 && onAction)
            onAction (i);
    }

private:
    class List final : public juce::Component
    {
    public:
        List (const Assets& a, int current) : assets (a), currentNote (current) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        std::function<void (int)> onNote;
        void paint (juce::Graphics& g) override
        {
            const auto clip = g.getClipBounds();
            const auto f = assets.font (kRowSize, kNoteValueSpacing);
            for (int n = juce::jmax (0, clip.getY() / rowHeight); n <= juce::jmin (127, clip.getBottom() / rowHeight); ++n)
            {
                const juce::Rectangle<int> r (0, n * rowHeight, getWidth(), rowHeight);
                if (n == currentNote || n == hover)
                {
                    g.setColour (n == currentNote ? palette::openOutline : palette::bronze);
                    g.drawRect (r.reduced (2, 1), 1);
                }
                const juce::String name = noteName (n), number (n);
                const auto area = r.toFloat();
                const float base = capBaseline (f, area);
                const auto bn = inkBounds (f, name), bm = inkBounds (f, number);
                drawInkText (g, f, name, { area.getX() + 14.0f, base + bn.getY() }, palette::label);
                drawInkText (g, f, number, { area.getRight() - 14.0f - bm.getWidth(), base + bm.getY() }, palette::secondary);
            }
        }
        void mouseMove (const juce::MouseEvent& e) override { setHover (e.y / rowHeight); }
        void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            const int n = e.y / rowHeight;
            if (e.mouseWasClicked() && n >= 0 && n <= 127 && onNote)
                onNote (n);
        }

    private:
        void setHover (int h)
        {
            if (h == hover)
                return;
            hover = h;
            repaint();
        }
        const Assets& assets;
        int currentNote, hover = -1;
    };

    juce::Rectangle<int> actionBounds (int i) const { return { inset, inset + i * actionHeight, width - 2 * inset, actionHeight }; }
    int actionAt (juce::Point<int> p) const
    {
        for (int i = 0; i < 3; ++i)
            if (actionBounds (i).contains (p))
                return i;
        return -1;
    }
    void setHover (int h)
    {
        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    const Assets& assets;
    List list;
    juce::Viewport viewport;
    int hover = -1;
};

// =====================================================================================
// A popup list or grid (Cymbalis's PopupPanel).
class PopupPanel final : public juce::Component
{
public:
    static constexpr int border = 2, padding = 6;
    PopupPanel (const Assets& a, juce::StringArray i, int sel, int cols, juce::Point<int> size)
        : assets (a), items (std::move (i)), selected (sel), columns (juce::jmax (1, cols)), itemSize (size)
    {
        const int rows = (items.size() + columns - 1) / columns;
        setSize (columns * itemSize.x + 2 * (border + padding), rows * itemSize.y + 2 * (border + padding));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    std::function<void (int)> onChoose;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (palette::popup);
        drawBoxOutline (g, getLocalBounds(), palette::boxOutline);
        const auto f = assets.font (kRowSize, kPopupSpacing);
        // a name wider than its cell (POPCORN SNARE in the drum menu) gets tighter letter spacing
        const auto tight = assets.font (kRowSize, 1.0f);
        for (int i = 0; i < items.size(); ++i)
        {
            const auto r = itemBounds (i);
            if (i == hover)
            {
                g.setColour (palette::bronze);
                g.drawRect (r.reduced (2), 1);
            }
            const bool fits = inkBounds (f, items[i]).getWidth() <= (float) r.getWidth() - 16.0f;
            drawValue (g, fits ? f : tight, items[i], r.toFloat(), selected < 0 || i == selected || i == hover ? palette::label : palette::secondary);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { setHover (itemAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = itemAt (e.getPosition());
        if (i >= 0 && onChoose)
            onChoose (i);
    }

private:
    juce::Rectangle<int> itemBounds (int index) const
    {
        const int in = border + padding;
        return { in + (index % columns) * itemSize.x, in + (index / columns) * itemSize.y, itemSize.x, itemSize.y };
    }
    int itemAt (juce::Point<int> p) const
    {
        for (int i = 0; i < items.size(); ++i)
            if (itemBounds (i).contains (p))
                return i;
        return -1;
    }
    void setHover (int h)
    {
        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }
    const Assets& assets;
    juce::StringArray items;
    int selected, columns;
    juce::Point<int> itemSize;
    int hover = -1;
};

namespace
{
// Catches clicks outside the popup and closes it.
class PopupLayer final : public juce::Component
{
public:
    explicit PopupLayer (std::function<void()> close) : onClose (std::move (close)) {}
    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClose)
            onClose();
    }

private:
    std::function<void()> onClose;
};
} // namespace

// =====================================================================================
// A page tab: DRUM, KIT or TUNING; the active one bright with a bronze line under it.
class TabButton final : public juce::Component
{
public:
    TabButton (const Assets& a, const char* t) : assets (a), text (t)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (t);
    }
    std::function<void()> onClick;
    void setActive (bool a)
    {
        active = a;
        repaint();
    }
    // the ink's width
    float inkWidth() const { return inkBounds (assets.font (kHeaderSize, kTabSpacing), text).getWidth(); }

    void paint (juce::Graphics& g) override
    {
        const auto f = assets.font (kHeaderSize, kTabSpacing);
        const auto c = active ? palette::label : (isMouseOver() ? palette::openOutline.withMultipliedBrightness (0.8f) : palette::secondary);
        drawInkText (g, f, text, { 0.0f, 6.0f }, c);
        if (active)
        {
            g.setColour (palette::bronze);
            g.fillRect (juce::Rectangle<float> (0.0f, 6.0f + 25.0f, inkWidth(), 2.0f));
        }
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }

private:
    const Assets& assets;
    juce::String text;
    bool active = false;
};

// =====================================================================================
// The SETTINGS page's MIDI MAP (Kris, 2026-11-26, mockups approved): the PRESET box (the map's
// name, " *" when changed since it was loaded or saved) and its list: GENERAL MIDI, the recent
// maps, LOAD... Maps are files wherever Kris saves them (no fixed folder, as Cymbalis); the
// recent ones and the last folder are kept in Tympanis's settings file.
class PresetBox final : public juce::Component
{
public:
    explicit PresetBox (const Assets& a) : assets (a)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle ("Preset");
    }
    std::function<void()> onClick;
    void setText (const juce::String& t)
    {
        text = t;
        repaint();
    }
    void setOpen (bool o)
    {
        open = o;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        drawBoxOutline (g, getLocalBounds(), open ? palette::openOutline : (isMouseOver() ? palette::bronze : palette::boxOutline));
        const auto f = assets.font (kRowSize, 2.4f);
        auto shown = text.toUpperCase();
        while (shown.length() > 3 && inkBounds (f, shown).getWidth() > (float) getWidth() - 46.0f)
            shown = shown.dropLastCharacters (2) + juce::String::charToString (0x2026);
        drawAt (g, f, shown, 13.0f, 7.0f, palette::label);
        juce::Path tri;
        const float tx = (float) getWidth() - 22.0f, ty = (float) getHeight() * 0.5f - 3.0f;
        tri.addTriangle (tx, ty, tx + 11.0f, ty, tx + 5.5f, ty + 7.0f);
        g.setColour (palette::arrow);
        g.fillPath (tri);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }

private:
    const Assets& assets;
    juce::String text;
    bool open = false;
};

class PresetList final : public juce::Component
{
public:
    enum Kind { map, chosen, separator, heading, action };
    struct Item
    {
        juce::String text;
        Kind kind;
        int id; // what onChoose gets: -1 General MIDI, 0.. a recent map, 1000 LOAD...
    };
    static constexpr int kItemH = 30;
    PresetList (const Assets& a, std::vector<Item> it) : assets (a), items (std::move (it)) {}
    std::function<void (int)> onChoose;
    int preferredHeight() const
    {
        int h = 14;
        for (const auto& i : items)
            h += i.kind == separator ? 11 : kItemH;
        return h;
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (palette::popup);
        drawBoxOutline (g, getLocalBounds(), palette::boxOutline, 1);
        int y = 8;
        for (size_t k = 0; k < items.size(); ++k)
        {
            const auto& i = items[k];
            if (i.kind == separator)
            {
                g.setColour (palette::divider);
                g.fillRect (10, y + 5, getWidth() - 20, 1);
                y += 11;
                continue;
            }
            const bool hover = (int) k == hovered && i.kind != heading;
            if (hover)
            {
                g.setColour (palette::bronze.withAlpha (0.25f));
                g.fillRect (4, y, getWidth() - 8, kItemH);
            }
            auto shown = i.text.toUpperCase();
            const auto f = assets.font (kRowSize, i.kind == heading ? 2.0f : 2.4f);
            while (shown.length() > 3 && inkBounds (f, shown).getWidth() > (float) getWidth() - 60.0f)
                shown = shown.dropLastCharacters (2) + juce::String::charToString (0x2026);
            drawAt (g, f, shown, 20.0f, (float) y + 9.0f,
                    i.kind == chosen || hover ? palette::label : (i.kind == heading ? palette::secondary.withAlpha (0.7f) : palette::secondary));
            if (i.kind == chosen)
            {
                juce::Path tick;
                tick.startNewSubPath ((float) getWidth() - 30.0f, (float) y + 15.0f);
                tick.lineTo ((float) getWidth() - 25.0f, (float) y + 20.0f);
                tick.lineTo ((float) getWidth() - 16.0f, (float) y + 9.0f);
                g.setColour (palette::bronze);
                g.strokePath (tick, juce::PathStrokeType (1.6f));
            }
            y += kItemH;
        }
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = itemAt (e.y);
        if (h != hovered)
        {
            hovered = h;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = -1;
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int k = itemAt (e.y);
        if (k >= 0 && items[(size_t) k].kind != heading && items[(size_t) k].kind != separator && onChoose)
            onChoose (items[(size_t) k].id);
    }

private:
    int itemAt (int py) const
    {
        int y = 8;
        for (size_t k = 0; k < items.size(); ++k)
        {
            const int h = items[k].kind == separator ? 11 : kItemH;
            if (py >= y && py < y + h)
                return (int) k;
            y += h;
        }
        return -1;
    }
    const Assets& assets;
    std::vector<Item> items;
    int hovered = -1;
};

// The recent maps (up to 8, newest first, files that still exist) and the folder last used, in
// Tympanis's settings file (Application Support/DHammers/Tympanis on a Mac).
juce::PropertiesFile& mapSettings()
{
    static juce::PropertiesFile file ([] {
        juce::PropertiesFile::Options o;
        o.applicationName = "Tympanis";
        o.folderName = "DHammers/Tympanis";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }());
    return file;
}
juce::Array<juce::File> recentMaps()
{
    juce::Array<juce::File> out;
    for (const auto& line : juce::StringArray::fromLines (mapSettings().getValue ("recentMaps")))
        if (line.isNotEmpty() && juce::File::isAbsolutePath (line) && juce::File (line).existsAsFile())
            out.addIfNotAlreadyThere (juce::File (line));
    return out;
}
void rememberMap (const juce::File& f)
{
    auto list = recentMaps();
    list.removeFirstMatchingValue (f);
    list.insert (0, f);
    juce::StringArray lines;
    for (int i = 0; i < juce::jmin (8, list.size()); ++i)
        lines.add (list[i].getFullPathName());
    mapSettings().setValue ("recentMaps", lines.joinIntoString ("\n"));
    mapSettings().setValue ("lastMapFolder", f.getParentDirectory().getFullPathName());
    mapSettings().saveIfNeeded();
}
juce::File lastMapFolder()
{
    const juce::String p = mapSettings().getValue ("lastMapFolder");
    if (p.isNotEmpty() && juce::File::isAbsolutePath (p) && juce::File (p).isDirectory())
        return juce::File (p);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

// =====================================================================================
// A finish on the SETTINGS page: its box (outlined in bronze when chosen) and name; its drum is
// a WireView over it. A click chooses it.
class FinishButton final : public juce::Component
{
public:
    FinishButton (const Assets& a, const char* t) : assets (a), text (t)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (t);
    }
    std::function<void()> onClick;
    void setChosen (bool c)
    {
        chosen = c;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        if (chosen)
        {
            g.setColour (palette::bronze);
            g.drawRect (getLocalBounds(), 2);
        }
        else
            drawBoxOutline (g, getLocalBounds(), isMouseOver() ? palette::bronze.withAlpha (0.6f) : palette::boxOutline, 1);
        drawAt (g, assets.font (kRowSize, 2.6f), text, (float) getWidth() * 0.5f, (float) getHeight() - 34.0f,
                chosen ? palette::label : (isMouseOver() ? palette::openOutline.withMultipliedBrightness (0.8f) : palette::secondary),
                Anchor::centre);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }

private:
    const Assets& assets;
    juce::String text;
    bool chosen = false;
};

// =====================================================================================
// RESET DRUM and RESET ALL on the TUNING page.
class FooterButton final : public juce::Component
{
public:
    FooterButton (const Assets& a, const char* t) : assets (a), text (t)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (t);
    }
    std::function<void()> onClick;
    void paint (juce::Graphics& g) override
    {
        drawBoxOutline (g, getLocalBounds(), isMouseButtonDown() ? palette::openOutline : (isMouseOver() ? palette::bronze : palette::boxOutline));
        drawValue (g, assets.font (kRowSize, 2.6f), text, getLocalBounds().toFloat(), palette::label);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        repaint();
        if (e.mouseWasClicked() && onClick)
            onClick();
    }

private:
    const Assets& assets;
    juce::String text;
};

// =====================================================================================
// A TUNING page row: the label, the slider (a bronze fill, from its centre for Tune and Pan),
// the value in its box (double-click to type one), the reset arrow; a bronze mark at the left
// when the value is not the default.
class TuneRow final : public juce::Component, private juce::Slider::Listener
{
public:
    enum class Format { semitones, plain, times, inches, decibels, pan, onOff };
    struct Spec
    {
        const char* suffix; // <drum key>_<suffix>, or a whole parameter ID
        bool perDrum;
        const char* label;
        Format format;
        bool bipolar;
        const char* leftEnd;
        const char* rightEnd;
    };

    TuneRow (const Assets& a, juce::AudioProcessorValueTreeState& s, const Spec& sp) : assets (a), state (s), spec (sp)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setScrollWheelEnabled (false);
        slider.setLookAndFeel (&look);
        slider.setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        slider.addListener (this);
        addAndMakeVisible (slider);
        look.row = this;
        setTitle (sp.label);
    }
    ~TuneRow() override
    {
        attachment.reset();
        slider.removeListener (this);
        slider.setLookAndFeel (nullptr);
    }

    // The parameter (per drum: that drum's) and the row's top, design units.
    void attach (const juce::String& id, int top, const juce::String& leftEnd = {}, const juce::String& rightEnd = {})
    {
        paramId = id;
        rowTop = top;
        ends[0] = leftEnd.isNotEmpty() ? leftEnd : juce::String (spec.leftEnd != nullptr ? spec.leftEnd : "");
        ends[1] = rightEnd.isNotEmpty() ? rightEnd : juce::String (spec.rightEnd != nullptr ? spec.rightEnd : "");
        setBounds (440, top - 6, 1060, 46);
        slider.setBounds (juce::roundToInt (kSliderX0 - kThumbRadius) - getX(), top + 1 - getY(), juce::roundToInt (x1() - kSliderX0 + 2 * kThumbRadius), 16);
        attachment.reset();
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
        if (auto* p = param())
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        cancelEdit();
        repaint();
    }

    juce::RangedAudioParameter* param() const { return state.getParameter (paramId); }
    float x1() const { return spec.format == Format::onOff ? kSliderX0 + 96.0f : kSliderX1; }

    // Greyed out: shown, not editable (Size on the drums whose depth does not play).
    void setGreyed (bool g)
    {
        if (g == greyed)
            return;
        greyed = g;
        cancelEdit();
        setAlpha (g ? 0.35f : 1.0f);
        setInterceptsMouseClicks (! g, ! g);
        slider.setEnabled (! g);
    }
    bool changed() const
    {
        auto* p = param();
        return p != nullptr && std::abs (p->getValue() - p->getDefaultValue()) > 1.0e-4f;
    }
    void reset()
    {
        if (auto* p = param())
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    }

    juce::String valueText() const
    {
        const double v = slider.getValue();
        switch (spec.format)
        {
            case Format::semitones: return (v >= 0 ? "+" : "") + juce::String (v, 2) + " st";
            case Format::plain: return juce::String (v, 2);
            case Format::times: return juce::String (v, 2) + " x";
            case Format::inches: return juce::String (v, 1) + " in";
            case Format::decibels: return levelText ((float) v);
            case Format::onOff: return v > 0.5 ? "ON" : "OFF";
            case Format::pan:
                if (std::abs (v) < 0.005)
                    return "C";
                return (v < 0 ? "L " : "R ") + juce::String (juce::roundToInt (std::abs (v) * 100.0));
        }
        return {};
    }

    void paint (juce::Graphics& g) override
    {
        g.addTransform (juce::AffineTransform::translation ((float) -getX(), (float) -getY()));
        const float y = (float) rowTop;
        const bool isChanged = changed();
        if (isChanged)
        {
            g.setColour (palette::bronze);
            g.fillRect (juce::Rectangle<float> (444.0f, y - 4.0f, 4.0f, 27.0f));
        }
        drawAt (g, assets.font (kRowSize, 2.6f), spec.label, 460.0f, y + 4.0f, palette::label);
        const auto endFont = assets.font (12.5f, 1.6f);
        drawAt (g, endFont, ends[0], kSliderX0, y + 24.0f, palette::secondary);
        drawAt (g, endFont, ends[1], x1(), y + 24.0f, palette::secondary, Anchor::right);
        // the value's box
        const auto box = kNumberBox.translated (0, rowTop);
        g.setColour (palette::popup);
        g.fillRect (box);
        drawBoxOutline (g, box, editor != nullptr ? palette::openOutline : palette::rowOutline, 1);
        if (editor == nullptr)
            drawAt (g, assets.font (kRowSize, 1.2f), valueText(), 1350.0f, y + 4.0f, palette::label, Anchor::right);
        // the reset arrow
        const juce::Point<float> c (kResetX, y + 9.0f);
        const auto colour = isChanged ? palette::bronze : palette::secondary;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, 8.0f, 8.0f, 0.0f, juce::degreesToRadians (-70.0f), juce::degreesToRadians (200.0f), true);
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (2.0f));
        juce::Path tri;
        tri.addTriangle (c.x + 3.0f, c.y - 12.0f, c.x + 9.0f, c.y - 8.0f, c.x + 3.0f, c.y - 3.0f);
        g.fillPath (tri);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto p = e.position + getPosition().toFloat();
        if (e.mouseWasClicked() && resetArea().contains (p))
            reset();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (kNumberBox.translated (0, rowTop).toFloat().contains (e.position + getPosition().toFloat()))
            startEdit();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto p = e.position + getPosition().toFloat();
        setMouseCursor (resetArea().contains (p) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

private:
    // The slider's track, fill, centre mark and thumb.
    struct Look final : public juce::LookAndFeel_V4
    {
        TuneRow* row = nullptr;
        int getSliderThumbRadius (juce::Slider&) override { return (int) kThumbRadius; }
        void drawLinearSlider (juce::Graphics& g, int x, int, int width, int, float pos, float, float, const juce::Slider::SliderStyle,
                               juce::Slider& s) override
        {
            const float cy = 8.0f;
            const auto track = juce::Rectangle<float> ((float) x, cy - 2.0f, (float) width, 4.0f);
            g.setColour (palette::rowOutline);
            g.fillRoundedRectangle (track, 2.0f);
            float from = (float) x;
            if (row->spec.bipolar)
            {
                from = (float) s.getPositionOfValue (0.0);
                g.setColour (palette::secondary);
                g.fillRect (juce::Rectangle<float> (from - 0.5f, cy - 6.0f, 1.0f, 12.0f));
            }
            g.setColour (palette::bronze);
            g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (std::min (from, pos), cy - 2.0f, std::max (from, pos), cy + 2.0f), 2.0f);
            g.setColour (s.isMouseButtonDown() ? palette::label : palette::openOutline);
            g.fillEllipse (pos - kThumbRadius, cy - kThumbRadius, 2 * kThumbRadius, 2 * kThumbRadius);
        }
    };

    juce::Rectangle<float> resetArea() const { return { kResetX - 13.0f, (float) rowTop - 5.0f, 26.0f, 28.0f }; }

    void sliderValueChanged (juce::Slider*) override { repaint(); }

    void startEdit()
    {
        editor = std::make_unique<juce::TextEditor>();
        editor->setFont (assets.font (kRowSize, 1.2f));
        editor->setJustification (juce::Justification::centredRight);
        editor->setColour (juce::TextEditor::backgroundColourId, palette::popup);
        editor->setColour (juce::TextEditor::textColourId, palette::label);
        editor->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        editor->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        editor->setColour (juce::TextEditor::highlightColourId, palette::bronze.withAlpha (0.6f));
        editor->setColour (juce::CaretComponent::caretColourId, palette::label);
        editor->setIndents (4, 3);
        editor->setText (valueText(), false);
        editor->selectAll();
        editor->onReturnKey = [this] { commitEdit(); };
        editor->onEscapeKey = [this] { cancelEdit(); };
        editor->onFocusLost = [this] { cancelEdit(); };
        editor->setBounds (kNumberBox.translated (-getX(), rowTop - getY()).reduced (2, 1));
        addAndMakeVisible (*editor);
        editor->grabKeyboardFocus();
        repaint();
    }
    void commitEdit()
    {
        if (editor == nullptr)
            return;
        const auto t = editor->getText().trim().toUpperCase();
        double v = t.getDoubleValue();
        if (spec.format == Format::pan)
        {
            if (t.startsWith ("C"))
                v = 0.0;
            else if (t.startsWith ("L") || t.startsWith ("R"))
                v = (t.startsWith ("L") ? -1.0 : 1.0) * t.substring (1).trim().getDoubleValue() / 100.0;
        }
        else if (spec.format == Format::decibels && t.contains ("INF"))
            v = TympanisProcessor::kLevelMinDb;
        else if (spec.format == Format::onOff && (t.startsWith ("ON") || t.startsWith ("OF")))
            v = t.startsWith ("ON") ? 1.0 : 0.0;
        if (auto* p = param(); p != nullptr && (t.containsAnyOf ("0123456789") || t.contains ("INF") || t.startsWith ("C") || t.startsWith ("O")))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit ((double) p->getNormalisableRange().start, (double) p->getNormalisableRange().end, v)));
            p->endChangeGesture();
        }
        cancelEdit();
    }
    void cancelEdit()
    {
        if (editor == nullptr)
            return;
        auto* e = editor.release();
        e->setVisible (false);
        juce::MessageManager::callAsync ([e] { delete e; }); // not from inside its own callback
        repaint();
    }

    const Assets& assets;
    juce::AudioProcessorValueTreeState& state;
    const Spec spec;
    Look look;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<juce::TextEditor> editor;
    juce::String paramId;
    juce::String ends[2];
    int rowTop = 0;
    bool greyed = false;
};

namespace
{
// The TUNING page's rows: this drum's, then the whole kit's (Kris, 2026-11-24).
const std::array<TuneRow::Spec, 7> kDrumTuneSpecs { {
    { "tune", true, "TUNE", TuneRow::Format::semitones, true, nullptr, nullptr },
    { "lug_detune", true, "LUG DETUNE", TuneRow::Format::plain, false, "EVEN", "UNEVEN" },
    { "decay", true, "DECAY", TuneRow::Format::times, false, "SHORT", "LONG" },
    { "strike_pos", true, "STRIKE POSITION", TuneRow::Format::plain, false, "CENTER", "RIM" },
    { "size", true, "SIZE", TuneRow::Format::inches, false, nullptr, nullptr },
    { "level", true, "LEVEL", TuneRow::Format::decibels, false, nullptr, nullptr },
    { "pan", true, "PAN", TuneRow::Format::pan, true, "L", "R" },
} };
const TuneRow::Spec kSnaresSpec { "snares", true, "SNARES", TuneRow::Format::onOff, false, "OFF", "ON" };
const TuneRow::Spec kWireSpec { "wire_tension", true, "WIRE TENSION", TuneRow::Format::plain, false, "TIGHT", "LOOSE" };
const std::array<TuneRow::Spec, 2> kKitTuneSpecs { {
    { "snare_buzz", false, "SNARE BUZZ", TuneRow::Format::plain, false, "OFF", "FULL" },
    { "main_volume", false, "MAIN VOLUME", TuneRow::Format::decibels, false, nullptr, nullptr },
} };
} // namespace

// =====================================================================================
Content::Content (TympanisProcessor& p)
    : processor (p),
      knobSpecs { {
          { "vel_sens", true, { 487.0f, 881.0f }, "VELOCITY SENSITIVITY", "-", "+", false },
          { "level", true, { 787.0f, 881.0f }, "LEVEL", "-", "+", false },
          { "pan", true, { 1087.0f, 881.0f }, "PAN", "L", "R", true },
          { "strike_pos", true, { 1349.0f, 642.0f }, "STRIKE POSITION", "CENTER", "RIM", false },
          { "main_volume", false, { 1350.0f, 881.0f }, "MAIN VOLUME", "-", "+", false },
          // SCRATCH mockup B: this drum's tuning on the DRUM page, a 2 x 2 grid in the left column
          { "tune", true, { 105.0f, 527.0f }, "TUNE", "LOW", "HIGH", true },
          { "lug_detune", true, { 300.0f, 527.0f }, "LUG DETUNE", "EVEN", "UNEVEN", false },
          { "decay", true, { 105.0f, 751.0f }, "DECAY", "SHORT", "LONG", false },
          { "size", true, { 300.0f, 751.0f }, "SIZE", "SHALLOW", "DEEP", false },
      } }
{
    setSize (kDesignWidth, kDesignHeight);
    setOpaque (true);
    processor.clearHits(); // hits from before the editor opened are not shown

    drumView = std::make_unique<WireView> (assets, WireView::Mode::drum);
    kitView = std::make_unique<WireView> (assets, WireView::Mode::kit);
    smallView = std::make_unique<WireView> (assets, WireView::Mode::small);
    for (auto* v : { drumView.get(), kitView.get(), smallView.get() })
        addChildComponent (*v);
    drumView->setBounds (WireView::area (WireView::Mode::drum));
    kitView->setBounds (WireView::area (WireView::Mode::kit));
    smallView->setBounds (WireView::area (WireView::Mode::small));
    kitView->onDrumClicked = [this] (int d) { selectDrum (d); };

    // SETTINGS: the finishes, Tom 3 in each
    const char* finishNames[] = { "GREEN", "NATURAL MAPLE" };
    int tom3 = 0;
    for (int d = 0; d < (int) kDrums.size(); ++d)
        if (juce::String (kDrums[(size_t) d].key) == "tom3")
            tom3 = d;
    for (int i = 0; i < 2; ++i)
    {
        finishButtons[(size_t) i] = std::make_unique<FinishButton> (assets, finishNames[i]);
        finishButtons[(size_t) i]->setBounds (460 + i * 270, 150, 250, 214);
        finishButtons[(size_t) i]->onClick = [this, i] { setFinish (i); };
        addChildComponent (*finishButtons[(size_t) i]);
        const juce::Rectangle<int> fa (468 + i * 270, 156, 234, 160);
        finishViews[(size_t) i] = std::make_unique<WireView> (assets, fa);
        finishViews[(size_t) i]->setBounds (fa);
        finishViews[(size_t) i]->setDrum (tom3);
        finishViews[(size_t) i]->setFinish (i == 1 ? wire::Finish::natural : wire::Finish::green);
        addChildComponent (*finishViews[(size_t) i]);
    }

    // SETTINGS: the MIDI MAP grid, the PRESET box and SAVE AS...
    for (size_t r = 0; r < kRows.size(); ++r)
    {
        auto box = std::make_unique<NoteBox> (assets);
        box->setBounds (gridCell ((int) r));
        box->onClick = [this, r] { openGridMenu ((int) r); };
        addChildComponent (*box);
        gridBoxes.push_back (std::move (box));
    }
    shownGridNotes.assign (kRows.size(), -2);
    presetBox = std::make_unique<PresetBox> (assets);
    presetBox->setBounds (1124, 396, 224, 26);
    presetBox->onClick = [this] { openPresetMenu(); };
    addChildComponent (*presetBox);
    saveAsButton = std::make_unique<FooterButton> (assets, "SAVE AS...");
    saveAsButton->setBounds (1358, 396, 132, 26);
    saveAsButton->onClick = [this] { saveMapAs(); };
    addChildComponent (*saveAsButton);

    // the tabs, 30 units apart, KIT first (Kris, 2026-11-27); tabs[] stays indexed by page
    const char* tabNames[] = { "DRUM", "KIT", "TUNING", "SETTINGS" };
    const size_t tabOrder[] = { kitPage, drumPage, tuningPage, settingsPage };
    float x = kTabLeft;
    for (size_t i : tabOrder)
    {
        tabs[i] = std::make_unique<TabButton> (assets, tabNames[i]);
        const float w = tabs[i]->inkWidth();
        tabs[i]->setBounds (juce::roundToInt (x), juce::roundToInt (kTabTop) - 6, juce::roundToInt (w) + 2, 36);
        tabs[i]->onClick = [this, i] { showPage ((int) i); };
        addAndMakeVisible (*tabs[i]);
        x += w + kTabGap;
    }

    drumBox = std::make_unique<DropdownBox> (assets, juce::Point<float> { 306.0f - (float) kDrumBox.getX(), 398.0f - (float) kDrumBox.getY() },
                                             DropdownBox::Align::centre);
    drumBox->setBounds (kDrumBox);
    drumBox->onClick = [this] { openDrumMenu(); };
    addChildComponent (*drumBox);
    channelBox = std::make_unique<DropdownBox> (assets, juce::Point<float> { 1484.0f - (float) kChannelBox.getX(), 44.0f - (float) kChannelBox.getY() },
                                                DropdownBox::Align::left);
    channelBox->setBounds (kChannelBox);
    channelBox->onClick = [this] { openChannelMenu(); };
    addAndMakeVisible (*channelBox);

    if (assets.logoTypeface != nullptr)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (assets.logoFont (kLogoSize), "DHammers", 0.0f, 0.0f);
        ga.createPath (logoPath);
        const auto ink = logoPath.getBounds();
        logoPath.applyTransform (juce::AffineTransform::translation (kLogoLeft - ink.getX(), kLogoMidY - ink.getCentreY()));
    }

    // DRUM page: knobs and note boxes
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        knobs[i] = std::make_unique<Knob> (knobSpecs[i].bipolar);
        const auto c = knobSpecs[i].centre;
        knobs[i]->setBounds (juce::roundToInt (c.x) - Knob::size / 2, juce::roundToInt (c.y) - Knob::size / 2, Knob::size, Knob::size);
        knobs[i]->setTitle (knobSpecs[i].label);
        knobs[i]->addListener (this);
        addChildComponent (*knobs[i]);
    }
    for (int b = 0; b < kMaxRows; ++b)
    {
        auto box = std::make_unique<NoteBox> (assets);
        box->setBounds (kNoteBox.withY (kRowTop[(size_t) b]));
        box->onNoteDragged = [this, b] (int n) {
            if ((size_t) b < drumRows.size())
                processor.setRowNote (drumRows[(size_t) b], n);
            refresh();
        };
        box->onClick = [this, b] { openNoteMenu (b); };
        addChildComponent (*box);
        noteBoxes[(size_t) b] = std::move (box);
    }
    shownNotes.fill (-2);

    // TUNING page
    for (const auto& spec : kDrumTuneSpecs)
    {
        drumTuneRows.push_back (std::make_unique<TuneRow> (assets, processor.params, spec));
        addChildComponent (*drumTuneRows.back());
    }
    snaresRow = std::make_unique<TuneRow> (assets, processor.params, kSnaresSpec);
    addChildComponent (*snaresRow);
    wireRow = std::make_unique<TuneRow> (assets, processor.params, kWireSpec);
    addChildComponent (*wireRow);
    for (size_t i = 0; i < kitTuneRows.size(); ++i)
    {
        kitTuneRows[i] = std::make_unique<TuneRow> (assets, processor.params, kKitTuneSpecs[i]);
        kitTuneRows[i]->attach (kKitTuneSpecs[i].suffix, kKitFirstRow + (int) i * kTuneRowStep);
        addChildComponent (*kitTuneRows[i]);
    }
    resetDrumButton = std::make_unique<FooterButton> (assets, "RESET DRUM");
    resetDrumButton->setBounds (1124, kButtonsY, 160, 34);
    resetDrumButton->onClick = [this] { resetParams (false); };
    addChildComponent (*resetDrumButton);
    resetAllButton = std::make_unique<FooterButton> (assets, "RESET ALL");
    resetAllButton->setBounds (1300, kButtonsY, 160, 34);
    resetAllButton->onClick = [this] { resetParams (true); };
    addChildComponent (*resetAllButton);

    addMouseListener (this, true); // a click anywhere cancels MIDI learn
    const int savedPage = processor.editorPage();
    page = savedPage;
    drum = -1;
    selectDrum (processor.selectedDrum());
    finish = -1;
    setFinish (processor.drumFinish());
    showPage (savedPage);
    refresh();
}

Content::~Content()
{
    closePopup();
    removeMouseListener (this);
    for (auto& k : knobs)
        k->removeListener (this);
    for (auto& a : knobAttachments)
        a.reset();
}

juce::String Content::paramId (const char* suffix) const
{
    return juce::String (kDrums[(size_t) drum].key) + "_" + suffix;
}

WireView* Content::wireView (int pg) const
{
    return pg == drumPage ? drumView.get() : (pg == kitPage ? kitView.get() : (pg == settingsPage ? finishViews[0].get() : smallView.get()));
}

void Content::showPage (int pg)
{
    closePopup();
    page = juce::jlimit (0, 3, pg);
    processor.setEditorPage (page);
    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i]->setActive ((int) i == page);
    const bool drumP = page == drumPage, kitP = page == kitPage, tuneP = page == tuningPage;
    drumView->setVisible (drumP);
    kitView->setVisible (kitP);
    smallView->setVisible (tuneP);
    const bool setP = page == settingsPage;
    for (size_t i = 0; i < finishViews.size(); ++i)
    {
        finishButtons[i]->setVisible (setP);
        finishViews[i]->setVisible (setP);
    }
    for (auto& b : gridBoxes)
        b->setVisible (setP);
    presetBox->setVisible (setP);
    saveAsButton->setVisible (setP);
    // the MIDI CHANNEL box: the drum's own on the DRUM page, every drum's on SETTINGS; none elsewhere
    channelBox->setVisible (drumP || setP);
    shownChannel = -1;
    drumBox->setVisible (drumP || tuneP);
    {
        static const bool mockB = std::getenv ("TYMP_MOCK") && std::getenv ("TYMP_MOCK")[0] == 'B';
        drumBox->setBounds (tuneP ? kDrumBox.translated (0, kTuneDrumDy) : mockB ? kDrumBox.translated (0, -78) : kDrumBox);
    }
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        static const bool mockB = std::getenv ("TYMP_MOCK") && std::getenv ("TYMP_MOCK")[0] == 'B';
        const juce::String suf (knobSpecs[i].suffix);
        const bool tuneKnob = i >= 5;
        knobs[i]->setVisible (drumP && (! tuneKnob || mockB) && (suf != "strike_pos" || kDrums[(size_t) drum].strikePos));
        const bool grey = (suf == "lug_detune" && juce::String (kDrums[(size_t) drum].key).startsWith ("kick")) || (suf == "size" && ! kDrums[(size_t) drum].depth);
        knobs[i]->setAlpha (grey ? 0.3f : 1.0f);
    }
    for (size_t b = 0; b < noteBoxes.size(); ++b)
        noteBoxes[b]->setVisible (drumP && b < drumRows.size());
    static const bool mockA = std::getenv ("TYMP_MOCK") && std::getenv ("TYMP_MOCK")[0] == 'A';
    for (size_t i = 0; i < drumTuneRows.size(); ++i)
        drumTuneRows[i]->setVisible (tuneP && (i != 3 || kDrums[(size_t) drum].strikePos) && ! (mockA && i >= 5));
    snaresRow->setVisible (tuneP && kDrums[(size_t) drum].engineOff >= 0);
    wireRow->setVisible (tuneP && kDrums[(size_t) drum].engineOff >= 0);
    for (auto& r : kitTuneRows)
        r->setVisible (tuneP);
    resetDrumButton->setVisible (tuneP);
    resetAllButton->setVisible (tuneP);
    repaint();
}

void Content::setFinish (int f)
{
    f = juce::jlimit (0, 1, f);
    if (f == finish)
        return;
    finish = f;
    processor.setDrumFinish (f);
    const auto wf = f == 1 ? wire::Finish::natural : wire::Finish::green;
    for (auto* v : { drumView.get(), kitView.get(), smallView.get() })
        v->setFinish (wf);
    for (size_t i = 0; i < finishButtons.size(); ++i)
        finishButtons[i]->setChosen ((int) i == f);
}

void Content::selectDrum (int d)
{
    d = juce::jlimit (0, (int) kDrums.size() - 1, d);
    if (d == drum)
        return;
    drum = d;
    processor.setSelectedDrum (d);
    drumView->setDrum (d);
    kitView->setDrum (d);
    smallView->setDrum (d);
    drumBox->setText (drumTitle (d));
    drumRows.clear();
    for (size_t r = 0; r < kRows.size(); ++r)
        if (kRows[r].drum == d)
            drumRows.push_back ((int) r);
    shownNotes.fill (-2);
    attachDrum();
    layoutTuningRows();
    showPage (page);
}

// The DRUM page's knobs to the selected drum's parameters.
void Content::attachDrum()
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        knobAttachments[i].reset();
        const auto& spec = knobSpecs[i];
        if (spec.perDrum && juce::String (spec.suffix) == "strike_pos" && ! kDrums[(size_t) drum].strikePos)
            continue; // the bass drums have none
        const juce::String id = spec.perDrum ? paramId (spec.suffix) : juce::String (spec.suffix);
        knobAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.params, id, *knobs[i]);
        if (auto* prm = processor.params.getParameter (id))
            knobs[i]->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }
}

// The TUNING page's drum rows, 56 apart from y 160 (no Strike Position on the bass drums).
void Content::layoutTuningRows()
{
    int top = kTuneFirstRow;
    const auto& info = kDrums[(size_t) drum];
    // the Snares On/Off switch and Wire Tension first, on the drums that have them (Kris, 2026-11-26)
    if (info.engineOff >= 0)
    {
        snaresRow->attach (paramId ("snares"), top);
        top += kDrumRowStep;
        wireRow->attach (paramId ("wire_tension"), top);
        top += kDrumRowStep;
    }
    for (size_t i = 0; i < drumTuneRows.size(); ++i)
    {
        if (i == 3 && ! info.strikePos)
            continue;
        const auto& spec = kDrumTuneSpecs[i];
        if (juce::String (spec.suffix) == "size")
        {
            // the shell's depth, -4 .. +2"; it plays on Toms 1-4 only (greyed out on the others)
            drumTuneRows[i]->attach (paramId (spec.suffix), top, sizeText (info.depthIn - kDepthDownIn), sizeText (info.depthIn + kDepthUpIn));
            drumTuneRows[i]->setGreyed (! info.depth);
        }
        else
            drumTuneRows[i]->attach (paramId (spec.suffix), top);
        // Lug Detune does nothing on the bass drums (Kris, 2026-11-27): greyed out there
        if (juce::String (spec.suffix) == "lug_detune")
            drumTuneRows[i]->setGreyed (juce::String (info.key).startsWith ("kick"));
        top += kDrumRowStep;
    }
}

bool Content::drumModified() const
{
    if (kDrums[(size_t) drum].engineOff >= 0 && (snaresRow->changed() || wireRow->changed()))
        return true;
    for (size_t i = 0; i < drumTuneRows.size(); ++i)
        if ((i != 3 || kDrums[(size_t) drum].strikePos) && drumTuneRows[i]->changed())
            return true;
    for (auto& r : kitTuneRows)
        if (r->changed())
            return true;
    return false;
}

void Content::resetParams (bool wholeKit)
{
    if (wholeKit)
    {
        for (auto* prm : processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (rp->getDefaultValue());
                rp->endChangeGesture();
            }
        return;
    }
    for (size_t i = 0; i < drumTuneRows.size(); ++i)
        if (i != 3 || kDrums[(size_t) drum].strikePos)
            drumTuneRows[i]->reset();
    if (kDrums[(size_t) drum].engineOff >= 0)
    {
        snaresRow->reset();
        wireRow->reset();
    }
}

// ---------------------------------------------------------------------------------------------
void Content::showHit (int row, float velocity, double ageSeconds)
{
    const auto& nr = kRows[(size_t) row];
    const double now = juce::Time::getMillisecondCounterHiRes();
    litUntilMs[(size_t) row] = now + 250.0 + 250.0 * velocity - ageSeconds * 1000.0;
    // where the stick lands: the head at its Strike Position (a bass drum's beater near the
    // centre), a rim shot at the edge, a side stick at the edge and softer
    double rpos = 0.3;
    float v = velocity;
    if (nr.art == Art::Head)
    {
        if (kDrums[(size_t) nr.drum].strikePos)
            if (auto* s = processor.params.getRawParameterValue (juce::String (kDrums[(size_t) nr.drum].key) + "_strike_pos"))
                rpos = s->load();
    }
    else if (nr.art == Art::Rim)
        rpos = 0.97;
    else
    {
        rpos = 0.9;
        v *= 0.5f;
    }
    if (page == drumPage)
        drumView->strike (nr.drum, v, rpos, ageSeconds);
    else if (page == kitPage)
    {
        kitView->strike (nr.drum, v, rpos, ageSeconds);
        kitHitDrum = nr.drum; // refresh() selects it (Kris, 2026-11-26)
    }
}

void Content::refresh()
{
    TympanisProcessor::HitEvent e;
    kitHitDrum = -1;
    while (processor.popHit (e))
        if (e.row >= 0 && (size_t) e.row < kRows.size())
            showHit (e.row, e.velocity);
    // the KIT page: a drum played becomes the selected drum (the last one this tick), so the DRUM
    // page shows it; not while a menu is open (selecting closes it)
    if (kitHitDrum >= 0 && popup == nullptr)
        selectDrum (kitHitDrum);

    // Size (the shell's depth) on Toms 1-4: the drawings follow it
    std::array<double, kDrums.size()> depthOff {};
    for (size_t d = 0; d < kDrums.size(); ++d)
        if (kDrums[d].depth)
            if (auto* v = processor.params.getRawParameterValue (juce::String (kDrums[d].key) + "_size"))
                depthOff[d] = (double) v->load() - kDrums[d].depthIn;
    if (depthOff != shownDepthOff)
    {
        shownDepthOff = depthOff;
        drumView->setDepths (depthOff);
        kitView->setDepths (depthOff);
        smallView->setDepths (depthOff);
        repaint (juce::Rectangle<int> (0, 430, 400, 40)); // the spec line
    }
    // the SNARES switches: the throw-offs' levers follow
    std::array<bool, kDrums.size()> snaresOff {};
    for (size_t d = 0; d < kDrums.size(); ++d)
        if (kDrums[d].engineOff >= 0)
            if (auto* v = processor.params.getRawParameterValue (juce::String (kDrums[d].key) + "_snares"))
                snaresOff[d] = v->load() < 0.5f;
    if (snaresOff != shownSnaresOff)
    {
        shownSnaresOff = snaresOff;
        drumView->setSnaresOff (snaresOff);
        kitView->setSnaresOff (snaresOff);
        smallView->setSnaresOff (snaresOff);
    }

    const double now = juce::Time::getMillisecondCounterHiRes();
    for (size_t b = 0; b < noteBoxes.size(); ++b)
    {
        if (b >= drumRows.size())
            continue;
        const int row = drumRows[b];
        const bool on = now < litUntilMs[(size_t) row];
        if (on != lit[(size_t) row])
        {
            lit[(size_t) row] = on;
            noteBoxes[b]->setLit (on);
            repaint (rowLabelArea ((int) b));
        }
        const int n = processor.getRowNote (row);
        if (n != shownNotes[b])
        {
            shownNotes[b] = n;
            noteBoxes[b]->setNote (n);
        }
    }
    // the MIDI CHANNEL box: the selected drum's own channel on the DRUM page (bronze when it
    // differs from the channel set for all), the channel set for all on SETTINGS
    const int all = processor.getMidiChannel();
    const int ch = page == drumPage ? processor.getDrumChannel (drum) : all;
    const int shownKey = ch * 2 + (page == drumPage && ch != all ? 1 : 0);
    if (shownKey != shownChannel)
    {
        shownChannel = shownKey;
        channelBox->setText (juce::String (ch));
        channelBox->setAccent (page == drumPage && ch != all);
    }
    // SETTINGS: the grid's notes, the map's name, the drums on their own channel
    if (page == settingsPage)
    {
        for (size_t r = 0; r < gridBoxes.size(); ++r)
            if (const int n = processor.getRowNote ((int) r); n != shownGridNotes[r])
            {
                shownGridNotes[r] = n;
                gridBoxes[r]->setNote (n);
            }
        const auto mapText = processor.mapName() + (processor.mapModified() ? " *" : "");
        if (mapText != shownMapText)
        {
            shownMapText = mapText;
            presetBox->setText (mapText);
        }
        const auto own = ownChannelsText();
        if (own != shownOwnChannels)
        {
            shownOwnChannels = own;
            repaint (440, 392, 600, 34);
        }
    }
    const int learn = processor.midiLearnRow();
    if (learn != shownLearn)
    {
        shownLearn = learn;
        learnStartMs = now;
    }
    const float pulse = 0.6f + 0.4f * (float) std::cos (juce::MathConstants<double>::twoPi * 1.25 * (now - learnStartMs) * 0.001);
    for (size_t b = 0; b < noteBoxes.size(); ++b)
        noteBoxes[b]->setLearning (b < drumRows.size() && drumRows[b] == learn, pulse);
    if (page == tuningPage)
    {
        const bool m = drumModified();
        if (m != shownModified)
        {
            shownModified = m;
            repaint (440, 820, 300, 50);
        }
    }
}

void Content::mouseDown (const juce::MouseEvent& e)
{
    const int learn = processor.midiLearnRow();
    if (learn < 0 || popup != nullptr)
        return;
    for (size_t b = 0; b < drumRows.size(); ++b)
        if (drumRows[b] == learn && e.eventComponent == noteBoxes[b].get())
            return;
    processor.cancelMidiLearn();
    refresh();
}

// ---------------------------------------------------------------------------------------------
juce::Rectangle<int> Content::knobLabelArea (int k) const
{
    const auto c = knobSpecs[(size_t) k].centre;
    return juce::Rectangle<float> (c.x - 150.0f, c.y - kKnobLabelAbove - 8.0f, 300.0f, 30.0f).toNearestInt();
}

void Content::sliderValueChanged (juce::Slider* s)
{
    if (draggingKnob >= 0 && knobs[(size_t) draggingKnob].get() == s)
        repaint (knobLabelArea (draggingKnob));
}

void Content::sliderDragStarted (juce::Slider* s)
{
    for (size_t i = 0; i < knobs.size(); ++i)
        if (knobs[i].get() == s)
            draggingKnob = (int) i;
    if (draggingKnob >= 0)
        repaint (knobLabelArea (draggingKnob));
}

void Content::sliderDragEnded (juce::Slider*)
{
    if (draggingKnob >= 0)
        repaint (knobLabelArea (draggingKnob));
    draggingKnob = -1;
}

void Content::showPopup (std::unique_ptr<juce::Component> panel, juce::Rectangle<int> bounds, std::function<void()> onClose)
{
    closePopup();
    popupClosed = std::move (onClose);
    popupLayer = std::make_unique<PopupLayer> ([this] { closePopup(); });
    popupLayer->setBounds (getLocalBounds());
    addAndMakeVisible (*popupLayer);
    popup = std::move (panel);
    popup->setBounds (bounds.constrainedWithin (getLocalBounds()));
    addAndMakeVisible (*popup);
}

void Content::closePopup()
{
    if (popup == nullptr)
        return;
    auto* p = popup.release();
    auto* l = popupLayer.release();
    p->setVisible (false);
    l->setVisible (false);
    juce::MessageManager::callAsync ([p, l] {
        delete p;
        delete l;
    });
    if (auto f = std::move (popupClosed))
        f();
    popupClosed = nullptr;
}

// The drums, two columns, below the DRUM box.
void Content::openDrumMenu()
{
    juce::StringArray names;
    for (const auto& d : kDrums)
        names.add (juce::String (d.name).toUpperCase());
    const int itemW = (kDrumBox.getWidth() - 2 * (PopupPanel::border + PopupPanel::padding)) / 2;
    auto panel = std::make_unique<PopupPanel> (assets, names, drum, 2, juce::Point<int> { itemW, 36 });
    panel->onChoose = [this] (int i) {
        closePopup();
        selectDrum (i);
    };
    const auto b = drumBox->getBounds().withY (drumBox->getBottom() + 4).withHeight (panel->getHeight());
    drumBox->setOpen (true);
    showPopup (std::move (panel), b, [this] { drumBox->setOpen (false); });
}

void Content::openChannelMenu()
{
    juce::StringArray items;
    for (int c = 1; c <= 16; ++c)
        items.add (juce::String (c));
    // the DRUM page: this drum's channel; SETTINGS: every drum's (it overrides their own)
    const bool one = page == drumPage;
    auto panel = std::make_unique<PopupPanel> (assets, items, (one ? processor.getDrumChannel (drum) : processor.getMidiChannel()) - 1, 4,
                                               juce::Point<int> { 61, 40 });
    panel->onChoose = [this, one] (int i) {
        if (one)
            processor.setDrumChannel (drum, i + 1);
        else
            processor.setMidiChannel (i + 1);
        closePopup();
        refresh();
    };
    const int w = panel->getWidth(), h = panel->getHeight();
    channelBox->setOpen (true);
    showPopup (std::move (panel), { kChannelBox.getRight() - w, kChannelBox.getBottom() + 3, w, h }, [this] { channelBox->setOpen (false); });
}

void Content::openNoteMenu (int b)
{
    if ((size_t) b >= drumRows.size())
        return;
    const int row = drumRows[(size_t) b];
    auto menu = std::make_unique<NoteMenu> (assets, processor.getRowNote (row));
    menu->onAction = [this, row] (int action) {
        closePopup();
        if (action == NoteMenu::learn)
            processor.startMidiLearn (row);
        else
        {
            processor.cancelMidiLearn();
            processor.setRowNote (row, action == NoteMenu::resetDefault ? kRows[(size_t) row].defaultNote : kNoteNone);
        }
        refresh();
    };
    menu->onNote = [this, row] (int note) {
        closePopup();
        processor.cancelMidiLearn();
        processor.setRowNote (row, note);
        refresh();
    };
    auto* box = noteBoxes[(size_t) b].get();
    const int w = menu->getWidth(), h = menu->getHeight();
    box->setOpen (true);
    showPopup (std::move (menu), { box->getRight() - w, box->getBottom() + 3, w, h }, [box] { box->setOpen (false); });
}

// ---------------------------------------------------------------------------------------------
void Content::paint (juce::Graphics& g)
{
    g.fillAll (palette::background);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (assets.background.isValid())
        g.drawImage (assets.background, getLocalBounds().toFloat());
    paintCommon (g);
    if (page == drumPage)
        paintDrumPage (g);
    else if (page == kitPage)
        paintKitPage (g);
    else if (page == settingsPage)
        paintSettingsPage (g);
    else
        paintTuningPage (g);
}

// The SETTINGS page (mockups approved 2026-11-26): DRUM FINISH, the two finish boxes (their own
// components); MIDI MAP: the drums on their own channel, PRESET and SAVE AS... (components), and
// the grid's labels (the note boxes are components).
void Content::paintSettingsPage (juce::Graphics& g)
{
    const auto band = [this, &g] (int y, const juce::String& title, const juce::String& note) {
        g.setColour (palette::popup);
        g.fillRect (440, y, 1060, 34);
        g.setColour (palette::bronze);
        g.fillRect (440, y + 33, 1060, 2);
        const auto f = assets.font (kHeaderSize, 3.3f);
        drawAt (g, f, title, 458.0f, (float) y + 10.0f, palette::label);
        // the note stops short of PRESET (x 1020): a long one ends in "..."
        const auto nf = assets.font (14.0f, 1.6f);
        const float x = 458.0f + inkBounds (f, title).getWidth() + 22.0f;
        auto shown = note;
        while (shown.length() > 3 && x + inkBounds (nf, shown).getWidth() > 1020.0f)
            shown = shown.dropLastCharacters (2) + juce::String::charToString (0x2026);
        drawAt (g, nf, shown, x, (float) y + 12.0f, palette::secondary);
    };
    band (100, "DRUM FINISH", "how the shells are drawn: a look, not a sound");
    band (392, "MIDI MAP", ownChannelsText());
    drawAt (g, assets.font (kRowSize, 3.0f), "PRESET", 1040.0f, 403.0f, palette::secondary);
    const auto hf = assets.font (kRowSize, 3.0f);
    drawAt (g, hf, "DRUM", 460.0f, 456.0f, palette::label);
    const char* cols[] = { "HEAD", "RIM SHOT", "SIDE STICK" };
    for (int c = 0; c < 3; ++c)
        drawAt (g, hf, cols[c], (float) gridCellX (c) + 100.0f, 456.0f, palette::label, Anchor::centre);
    g.setColour (palette::divider);
    g.fillRect (452, 482, 1036, 1);
    for (size_t d = 0; d < kDrums.size(); ++d)
    {
        const int y = gridRowY ((int) d);
        drawAt (g, assets.font (kRowSize, 2.6f), juce::String (kDrums[d].name).toUpperCase(), 460.0f, (float) y + 8.0f, palette::secondary);
        for (int c = 0; c < 3; ++c)
        {
            bool has = false;
            for (const auto& r : kRows)
                has = has || (r.drum == (int) d && (int) r.art == c);
            if (! has)
                drawAt (g, hf, kEmDash, (float) gridCellX (c) + 100.0f, (float) y + 13.0f, palette::secondary.withAlpha (0.4f), Anchor::centre);
        }
        if (d + 1 < kDrums.size())
        {
            g.setColour (palette::divider.withAlpha (0.5f));
            g.fillRect (452, y + 29, 1036, 1);
        }
    }
}

// The MIDI MAP grid: a row per drum from y 492, 30 apart; HEAD, RIM SHOT, SIDE STICK columns
// 240 apart from x 790 (200 wide), a 96 x 25 note box in the middle of each.
int Content::gridRowY (int d) { return 492 + d * 30; }
int Content::gridCellX (int c) { return 790 + c * 240; }
juce::Rectangle<int> Content::gridCell (int row)
{
    return { gridCellX ((int) kRows[(size_t) row].art) + 52, gridRowY (kRows[(size_t) row].drum) + 1, 96, 25 };
}

juce::String Content::ownChannelsText() const
{
    juce::StringArray own;
    for (size_t d = 0; d < kDrums.size(); ++d)
        if (processor.getDrumChannel ((int) d) != processor.getMidiChannel())
            own.add (juce::String (kDrums[d].name) + " " + juce::String (processor.getDrumChannel ((int) d)));
    if (own.isEmpty())
        return "every drum on the channel set at the top right";
    return juce::String (own.size()) + (own.size() == 1 ? " drum on its own channel: " : " drums on their own channels: ") + own.joinIntoString (", ");
}

void Content::openGridMenu (int row)
{
    auto menu = std::make_unique<NoteMenu> (assets, processor.getRowNote (row));
    menu->onAction = [this, row] (int action) {
        closePopup();
        if (action == NoteMenu::learn)
            processor.startMidiLearn (row);
        else
            processor.setRowNote (row, action == NoteMenu::resetDefault ? kRows[(size_t) row].defaultNote : kNoteNone);
        refresh();
    };
    menu->onNote = [this, row] (int note) {
        closePopup();
        processor.setRowNote (row, note);
        refresh();
    };
    auto* box = gridBoxes[(size_t) row].get();
    const int w = menu->getWidth(), h = menu->getHeight();
    const int y = box->getBottom() + 3 + h <= kDesignHeight ? box->getBottom() + 3 : box->getY() - 3 - h; // up near the bottom
    box->setOpen (true);
    showPopup (std::move (menu), { box->getRight() - w, y, w, h }, [box] { box->setOpen (false); });
}

void Content::openPresetMenu()
{
    std::vector<PresetList::Item> items;
    const auto file = processor.mapFile();
    items.push_back ({ "General MIDI", file == juce::File() ? PresetList::chosen : PresetList::map, -1 });
    const auto recent = recentMaps();
    if (! recent.isEmpty())
    {
        items.push_back ({ "", PresetList::separator, 0 });
        items.push_back ({ "Recent", PresetList::heading, 0 });
        for (int i = 0; i < recent.size(); ++i)
            items.push_back ({ recent[i].getFileNameWithoutExtension(), recent[i] == file ? PresetList::chosen : PresetList::map, i });
    }
    items.push_back ({ "", PresetList::separator, 0 });
    items.push_back ({ "Load...", PresetList::action, 1000 });
    auto list = std::make_unique<PresetList> (assets, std::move (items));
    list->onChoose = [this, recent] (int id) {
        closePopup();
        if (id == -1)
            processor.useGeneralMidi();
        else if (id == 1000)
            loadMap();
        else if (id >= 0 && id < recent.size())
            loadMapFile (recent[id]);
        refresh();
    };
    const int h = list->preferredHeight();
    presetBox->setOpen (true);
    showPopup (std::move (list), { presetBox->getX(), presetBox->getBottom() + 3, 250, h }, [this] { presetBox->setOpen (false); });
}

void Content::loadMapFile (const juce::File& f)
{
    if (processor.loadMapFile (f))
        rememberMap (f);
    else
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Tympanis", "That file is not a Tympanis MIDI map:\n" + f.getFullPathName());
    refresh();
}

void Content::loadMap()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a MIDI map", lastMapFolder(), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        if (fc.getResult() != juce::File())
            loadMapFile (fc.getResult());
    });
}

void Content::saveMapAs()
{
    const auto name = processor.mapFile() == juce::File() ? juce::String ("My MIDI map") : processor.mapName();
    chooser = std::make_unique<juce::FileChooser> ("Save the MIDI map", lastMapFolder().getChildFile (name + ".json"), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              if (! f.hasFileExtension ("json"))
                                  f = f.withFileExtension ("json");
                              if (processor.saveMapFile (f))
                                  rememberMap (f);
                              else
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Tympanis",
                                                                          "Could not save the MIDI map:\n" + f.getFullPathName());
                              refresh();
                          });
}

void Content::paintCommon (juce::Graphics& g)
{
    const auto header = assets.font (kHeaderSize, 3.11f);
    const auto ink = inkBounds (header, "MIDI CHANNEL");
    if (page == drumPage || page == settingsPage) // the MIDI CHANNEL box shows on these two only
        drawInkText (g, header, "MIDI CHANNEL", { kChannelLabelRight - ink.getWidth(), kChannelBox.toFloat().getCentreY() - ink.getHeight() * 0.5f },
                     palette::label);
    if (page == drumPage || page == tuningPage)
    {
        const bool tune = page == tuningPage;
        static const bool mockB = std::getenv ("TYMP_MOCK") && std::getenv ("TYMP_MOCK")[0] == 'B';
        const float dy = tune ? (float) kTuneDrumDy : mockB ? -78.0f : 0.0f;
        drawAt (g, assets.font (kHeaderSize, 3.45f), "DRUM", 199.5f, 348.0f + dy, palette::label, Anchor::centre);
        drawAt (g, assets.font (kRowSize, 2.4f), drumSpecLine (drum, kDrums[(size_t) drum].depth ? kDrums[(size_t) drum].depthIn + shownDepthOff[(size_t) drum] : -1.0),
                199.5f, tune ? kTuneSpecY : 452.0f + dy, palette::secondary, Anchor::centre);
    }
    if (! logoPath.isEmpty())
    {
        const auto b = logoPath.getBounds();
        g.setGradientFill (juce::ColourGradient::vertical (palette::logoTop, b.getY(), palette::logoBottom, b.getBottom()));
        g.fillPath (logoPath);
    }
}

juce::Rectangle<int> Content::rowLabelArea (int b) const
{
    return { 1196, kRowTop[(size_t) b] - 2, 220, 29 };
}

void Content::paintDrumPage (juce::Graphics& g)
{
    g.setColour (palette::divider);
    {
        static const bool mockB = std::getenv ("TYMP_MOCK") && std::getenv ("TYMP_MOCK")[0] == 'B';
        const int x0 = mockB ? 400 : 39;    // SCRATCH mockup B: the left column is the drum's own block
        g.fillRect (x0, 757, 1161 - x0, 1);
    }
    g.fillRect (1188, 110, 1, 850 - 110);

    const auto header = [this, &g] (const char* text, float x, float y, float spacing) {
        drawInkText (g, assets.font (kHeaderSize, spacing), text, { x, y }, palette::label);
    };
    header ("ARTICULATION", 1201.0f, 98.0f, 3.29f);
    header ("MIDI NOTE", 1424.0f, 98.0f, 3.0f);
    for (size_t b = 0; b < drumRows.size() && b < (size_t) kMaxRows; ++b)
    {
        const int row = drumRows[b];
        drawInkText (g, assets.font (kRowSize, 3.0f), artLabel (kRows[(size_t) row].art), { 1201.0f, (float) kRowTop[b] + 8.0f },
                     lit[(size_t) row] ? palette::openOutline : palette::secondary);
    }

    // knob labels (a dragged knob's readout in its place) and their end marks
    const auto endFont = assets.font (kRowSize, 2.0f);
    for (size_t i = 0; i < knobSpecs.size(); ++i)
    {
        if (! knobs[i]->isVisible())
            continue;
        const auto& spec = knobSpecs[i];
        const auto c = spec.centre;
        // VELOCITY SENSITIVITY at Cymbalis's spacing (3.09); the others 3.3
        const auto f = assets.font (kHeaderSize, juce::String (spec.label).length() > 16 ? 3.09f : 3.3f);
        juce::String text (spec.label);
        if ((int) i == draggingKnob)
        {
            const juce::String id = spec.perDrum ? paramId (spec.suffix) : juce::String (spec.suffix);
            if (auto* prm = processor.params.getParameter (id))
            {
                text = prm->getCurrentValueAsText();
                if (juce::String (spec.suffix) == "level" || juce::String (spec.suffix) == "main_volume")
                    text = levelText (prm->convertFrom0to1 (prm->getValue()));
            }
        }
        const float a = knobs[i]->getAlpha();
        drawAt (g, f, text, c.x, c.y - kKnobLabelAbove, palette::knobLabel.withMultipliedAlpha (a), Anchor::centre);
        g.setColour (palette::secondary);
        if (i >= 5)
        {
            // SCRATCH mockup B: the end labels pulled in (the grid is 195 apart)
            drawAt (g, endFont, spec.leftEnd, c.x - 58.0f, c.y + kKnobEndBelow, palette::secondary.withMultipliedAlpha (a), Anchor::centre);
            drawAt (g, endFont, spec.rightEnd, c.x + 58.0f, c.y + kKnobEndBelow, palette::secondary.withMultipliedAlpha (a), Anchor::centre);
        }
        else if (juce::String (spec.leftEnd) == "-")
        {
            // - and + (Cymbalis's marks)
            g.fillRect (juce::Rectangle<float> (c.x - 81.0f, c.y + 81.0f, 11.0f, 1.0f));
            g.fillRect (juce::Rectangle<float> (c.x + 73.0f - 5.0f, c.y + 82.0f - 0.5f, 10.0f, 1.0f));
            g.fillRect (juce::Rectangle<float> (c.x + 73.0f - 0.5f, c.y + 82.0f - 4.5f, 1.0f, 9.0f));
        }
        else
        {
            drawAt (g, endFont, spec.leftEnd, c.x - kKnobEndSide, c.y + kKnobEndBelow, palette::secondary, Anchor::centre);
            drawAt (g, endFont, spec.rightEnd, c.x + kKnobEndSide, c.y + kKnobEndBelow, palette::secondary, Anchor::centre);
        }
    }
}

void Content::paintKitPage (juce::Graphics& g)
{
    drawAt (g, assets.font (kRowSize, 3.0f), "SELECTED", 1514.0f, 104.0f, palette::secondary, Anchor::right);
    drawAt (g, assets.font (kHeaderSize, 3.2f), drumTitle (drum), 1514.0f, 128.0f, palette::label, Anchor::right);
    if (wire::kitItemForDrum (drum) < 0)
        drawAt (g, assets.font (12.5f, 1.6f), "NOT SHOWN IN THE KIT YET", 1514.0f, 156.0f, palette::secondary, Anchor::right);
}

void Content::paintTuningPage (juce::Graphics& g)
{
    const auto band = [this, &g] (int y, const juce::String& title, const char* note) {
        g.setColour (palette::popup);
        g.fillRect (440, y, 1060, 34);
        g.setColour (palette::bronze);
        g.fillRect (440, y + 33, 1060, 2);
        const auto f = assets.font (kHeaderSize, 3.3f);
        drawAt (g, f, title, 458.0f, (float) y + 10.0f, palette::label);
        drawAt (g, assets.font (14.0f, 1.6f), note, 458.0f + inkBounds (f, title).getWidth() + 22.0f, (float) y + 12.0f, palette::secondary);
    };
    band (100, juce::String (kDrums[(size_t) drum].name).toUpperCase(), "affects only this drum");
    band (kKitBandTop, "WHOLE KIT", "affects every drum");
    if (shownModified)
        drawAt (g, assets.font (kRowSize, 2.6f), "MODIFIED", 460.0f, 841.0f, palette::bronze);
}

} // namespace tympanis::ui

// =====================================================================================
TympanisEditor::TympanisEditor (TympanisProcessor& p) : AudioProcessorEditor (&p), processorRef (p)
{
    using namespace tympanis::ui;
    int w = p.editorSize().x;
    contentComponent = std::make_unique<Content> (p);
    addAndMakeVisible (*contentComponent);

    // resizable at a fixed 3:2, from 960 x 640 to 1536 x 1024 (as Cymbalis)
    setResizable (true, true);
    setResizeLimits (kEditorMinWidth, kEditorMinHeight, kDesignWidth, kDesignHeight);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);
    setLookAndFeel (&resizerLookAndFeel);

    // The saved size, or 1104 x 736, smaller where the primary display leaves no room for a
    // host's window around it (editorOpeningWidth, as Cymbalis). It used to open as large as
    // the screen allowed (its height - 80), which ran past the bottom in hosts with their own
    // title and preset bars.
    if (w < kEditorMinWidth)
    {
        juce::Rectangle<int> area (0, 0, kDesignWidth, kDesignHeight);
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            area = d->userArea;
        w = editorOpeningWidth (area.getWidth(), area.getHeight(), kDesignWidth);
    }
    w = juce::jlimit (kEditorMinWidth, kDesignWidth, w);
    sizeIsSet = true;
    setSize (w, juce::roundToInt (w * (double) kDesignHeight / (double) kDesignWidth));
    startTimerHz (30);
}

TympanisEditor::~TympanisEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TympanisEditor::paint (juce::Graphics& g)
{
    g.fillAll (tympanis::ui::palette::background);
}

void TympanisEditor::resized()
{
    using namespace tympanis::ui;
    const float scale = juce::jmin ((float) getWidth() / (float) kDesignWidth, (float) getHeight() / (float) kDesignHeight);
    contentComponent->setTransform (juce::AffineTransform::scale (scale).translated (((float) getWidth() - scale * (float) kDesignWidth) * 0.5f,
                                                                                     ((float) getHeight() - scale * (float) kDesignHeight) * 0.5f));
    contentComponent->setBounds (0, 0, kDesignWidth, kDesignHeight);
    if (sizeIsSet)
        processorRef.setEditorSize (getWidth(), getHeight());
}

void TympanisEditor::timerCallback()
{
    contentComponent->refresh();
}

void TympanisEditor::ResizerLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    using namespace tympanis::ui;
    g.setColour (isMouseDragging ? palette::openOutline : (isMouseOver ? palette::bronze : palette::boxOutline));
    const float s = (float) juce::jmin (w, h);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine ((float) w - s * i, (float) h, (float) w, (float) h - s * i, 1.0f);
}
