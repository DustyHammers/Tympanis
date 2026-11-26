#include "WireView.h"

#include <algorithm>
#include <cmath>

namespace tympanis::ui
{

namespace
{
// The DRUM page's drum: its bounds at most 560 x 520, centred on 787, 400 (Tom 3 as the approved
// mockup: 520 high). The TUNING page's small drum: 230 x 218 on 199.5, 600 (Tom 3 0.42 x that).
constexpr wire::Pt kDrumCentre { 787.0f, 400.0f }, kSmallCentre { 199.5f, 600.0f };
constexpr double kDrumMaxW = 560, kDrumMaxH = 520, kSmallMaxW = 230, kSmallMaxH = 218;

bool insideConvex (const std::vector<wire::Pt>& poly, juce::Point<float> p)
{
    if (poly.size() < 3)
        return false;
    int sign = 0;
    for (size_t i = 0; i < poly.size(); ++i)
    {
        const auto a = poly[i], b = poly[(i + 1) % poly.size()];
        const double c = (double) (b.x - a.x) * (p.y - a.y) - (double) (b.y - a.y) * (p.x - a.x);
        const int s = c > 0 ? 1 : (c < 0 ? -1 : 0);
        if (s == 0)
            continue;
        if (sign == 0)
            sign = s;
        else if (s != sign)
            return false;
    }
    return true;
}

inline std::uint8_t clip8 (float v) noexcept
{
    return v >= 254.5f ? (std::uint8_t) 255 : (v > 0.0f ? (std::uint8_t) (v + 0.5f) : (std::uint8_t) 0);
}
} // namespace

WireView::WireView (const Assets& a, Mode m) : assets (a), mode (m), designArea (area (m))
{
    setOpaque (true);
    setInterceptsMouseClicks (mode == Mode::kit, false);
}

WireView::WireView (const Assets& a, juce::Rectangle<int> areaIn) : assets (a), mode (Mode::small), designArea (areaIn), placed (true)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
}

WireView::~WireView()
{
    stopTimer();
}

juce::Rectangle<int> WireView::area (Mode m)
{
    switch (m)
    {
        case Mode::drum: return { 400, 40, 780, 710 };  // inside the right column's divider and above the bottom row's
        case Mode::small: return { 40, 476, 320, 250 }; // the left column, under the spec line
        case Mode::kit: break;
    }
    return { 200, 300, 1180, 640 };
}

void WireView::setFinish (wire::Finish f)
{
    if (f == finish)
        return;
    finish = f;
    needsBuild = true;
    repaint();
}

void WireView::setDrum (int d)
{
    if (d == shownDrum)
        return;
    shownDrum = d;
    if (mode == Mode::kit)
    {
        // only the selection's highlight changes
        if (! needsBuild && frame.isValid())
            applySelection();
        return;
    }
    numHits = 0; // another drum: the last one's hits go
    needsBuild = true;
    repaint();
}

void WireView::setDepths (const std::array<double, wire::kDrumCount>& offsetsIn)
{
    if (offsetsIn == depthOff)
        return;
    const bool shownChanged = offsetsIn[(size_t) juce::jlimit (0, wire::kDrumCount - 1, shownDrum)] != depthOff[(size_t) juce::jlimit (0, wire::kDrumCount - 1, shownDrum)];
    depthOff = offsetsIn;
    if (mode != Mode::kit && ! shownChanged)
        return;
    numHits = 0;
    needsBuild = true;
    repaint();
}

void WireView::strike (int d, float velocity, double rpos, double ageSeconds)
{
    if (mode == Mode::small)
        return;
    int item = -1;
    if (mode == Mode::drum)
        item = d == shownDrum ? 0 : -1;
    else if (const int k = wire::kitItemForDrum (d); k >= 0)
    {
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].kitIndex == k)
                item = (int) i;
        if (items.empty())
            item = -2 - k; // found once the kit is built (paint)
    }
    if (item == -1)
        return;
    // where the stick lands: the side nearest the drummer, give or take 35 degrees
    double near = 0.0;
    if (item >= 0 && (size_t) item < items.size())
        near = items[(size_t) item].drawing.head.nearAngle();
    const double angle = near + (random.nextDouble() - 0.5) * 1.2;
    const double now = juce::Time::getMillisecondCounterHiRes();
    ActiveHit h { item, now - ageSeconds * 1000.0, (double) juce::jlimit (0.0f, 1.0f, velocity), juce::jlimit (0.0, 1.0, rpos), angle };
    if (numHits < (int) hits.size())
        hits[(size_t) numHits++] = h;
    else
        *std::min_element (hits.begin(), hits.end(), [] (const ActiveHit& a, const ActiveHit& b) { return a.startMs < b.startMs; }) = h;
    if (! needsBuild && frame.isValid())
    {
        drawFrame (now);
        if (! isTimerRunning())
            startTimerHz (30);
    }
    else
        repaint();
}

void WireView::renderNow (float scale)
{
    if (needsBuild || std::abs (scale - builtScale) > 1.0e-3f)
        build (scale);
    drawFrame (juce::Time::getMillisecondCounterHiRes());
}

void WireView::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (needsBuild || std::abs (scale - builtScale) > 1.0e-3f || ! frame.isValid())
    {
        build (scale);
        drawFrame (juce::Time::getMillisecondCounterHiRes());
        if (numHits > 0 && ! isTimerRunning())
            startTimerHz (30);
    }
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (frame, juce::Rectangle<float> (0.0f, 0.0f, (float) pw / builtScale, (float) ph / builtScale));
}

void WireView::timerCallback()
{
    drawFrame (juce::Time::getMillisecondCounterHiRes());
    if (numHits == 0)
        stopTimer();
}

// ---------------------------------------------------------------------------------------------
juce::Rectangle<int> WireView::pixelBox (float x0, float y0, float x1, float y1, float margin) const
{
    const auto design = juce::Rectangle<float>::leftTopRightBottom (x0 - margin, y0 - margin, x1 + margin, y1 + margin)
                            .translated ((float) -designArea.getX(), (float) -designArea.getY());
    return (design * builtScale).getSmallestIntegerContainer().getIntersection ({ 0, 0, pw, ph });
}

// The canvas over a box of the view (pixels).
void WireView::boxCanvas (juce::Rectangle<int> b)
{
    canvas.reset (b.getWidth(), b.getHeight(), (float) designArea.getX() + (float) b.getX() / builtScale,
                  (float) designArea.getY() + (float) b.getY() / builtScale, builtScale);
}

// What the kit drums in front of an item leave of it over a box: the product of (1 - 0.93
// coverage), as the still drawing darkens it.
std::vector<float> WireView::keepFor (size_t i, juce::Rectangle<int> b)
{
    boxCanvas (b);
    std::vector<float> keep ((size_t) b.getWidth() * (size_t) b.getHeight(), 1.0f);
    for (size_t j = i + 1; j < items.size(); ++j)
        canvas.occlude (items[j].drawing.silhouette, 0.93f, keep);
    return keep;
}

// A drum's lines into the canvas: its own, then the side layer only outside its body; and its
// head at rest.
void WireView::drawDrumLines (const wire::DrumDrawing& dr, bool withHead)
{
    canvas.draw (dr.segments, false);
    mask.resize ((size_t) canvas.width() * (size_t) canvas.height(), 0.0f);
    canvas.fillMask (dr.bodyHull, mask);
    canvas.draw (dr.segments, true, mask.data());
    canvas.clearMask (dr.bodyHull, mask);
    if (withHead && dr.headVisible)
    {
        segs.clear();
        flashes.clear();
        dr.head.draw (nullptr, 0, segs, flashes);
        canvas.draw (segs, false);
    }
}

void WireView::build (float scale)
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    builtScale = std::max (0.05f, scale);
    needsBuild = false;
    pw = std::max (1, juce::roundToInt ((float) designArea.getWidth() * builtScale));
    ph = std::max (1, juce::roundToInt ((float) designArea.getHeight() * builtScale));
    canvas.reset (pw, ph, (float) designArea.getX(), (float) designArea.getY(), builtScale);
    mask.assign ((size_t) pw * (size_t) ph, 0.0f);

    items.clear();
    if (mode == Mode::kit)
    {
        const auto cam = wire::kitCamera();
        const auto& layout = wire::kitLayout();
        for (size_t k = 0; k < layout.size(); ++k)
        {
            Item it;
            it.drum = layout[k].drum;
            it.kitIndex = (int) k;
            it.drawing = wire::drawDrum (wire::kitSpec (layout[k], depthOff[(size_t) layout[k].drum]), cam, 0.0, finish);
            if (layout[k].spec.kind == wire::Kind::kick)
            {
                // its pedal: the fixed parts with the drum; the moving parts over the frame
                wire::addKickPedal (it.drawing, layout[k].spec, cam, 0.0, 0.0, wire::PedalParts::fixed);
                it.pedal = true;
                it.pedalKick = layout[k].spec;
            }
            it.drawing.depth += layout[k].depthBias;
            items.push_back (std::move (it));
        }
        std::stable_sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.drawing.depth > b.drawing.depth; });
    }
    else
    {
        const bool big = mode == Mode::drum;
        // the DRUM page: every drum at one scale (proportional to each other); the small one: its own
        // a view placed elsewhere (the SETTINGS page's) frames its drum in its own area
        const wire::Pt smallC = placed ? wire::Pt { (float) designArea.getCentreX(), (float) designArea.getCentreY() } : kSmallCentre;
        const double smallW = placed ? designArea.getWidth() * 0.78 : kSmallMaxW, smallH = placed ? designArea.getHeight() * 0.9 : kSmallMaxH;
        const auto fr = wire::frameDrum (shownDrum, big ? kDrumCentre : smallC, big ? kDrumMaxW : smallW, big ? kDrumMaxH : smallH,
                                         depthOff[(size_t) juce::jlimit (0, wire::kDrumCount - 1, shownDrum)], big ? wire::Scale::kit : wire::Scale::own);
        Item it;
        it.drum = shownDrum;
        it.drawing = wire::drawDrum (fr.spec, fr.cam, 0.0, finish);
        items.push_back (std::move (it));
    }

    // far first: each kit drum hides what is behind it, then its lines (the DRUM page's head is
    // drawn with the moving frames)
    for (auto& it : items)
    {
        if (mode == Mode::kit)
            canvas.darken (it.drawing.silhouette, 0.93f);
        drawDrumLines (it.drawing, mode != Mode::drum);
        const auto& h = it.drawing.head;
        it.box = it.drawing.headVisible ? pixelBox (h.x0, h.y0, h.x1, h.y1, 2.0f * h.flexReach + 60.0f) : juce::Rectangle<int>();
        it.keepReady = false;
        if (it.pedal)
        {
            // the box the moving parts can reach, from rest to against the head
            wire::DrumDrawing reach;
            reach.x0 = reach.y0 = 1e9f;
            reach.x1 = reach.y1 = -1e9f;
            for (double s : { 0.0, 0.5, 1.0 })
                wire::addKickPedal (reach, it.pedalKick, wire::kitCamera(), 1.0, s, wire::PedalParts::moving);
            it.pedalBox = pixelBox (reach.x0, reach.y0, reach.x1, reach.y1, 10.0f);
            it.pedalKeepReady = false;
            it.pedalDrawn = 0.0;
        }
    }
    canvas.bloom();

    // over the background: the brighter of the two
    juce::Image bg (juce::Image::ARGB, pw, ph, true);
    {
        juce::Graphics g (bg);
        g.fillAll (palette::background);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        if (assets.background.isValid())
            g.drawImageTransformed (assets.background,
                                    juce::AffineTransform::translation ((float) -designArea.getX(), (float) -designArea.getY()).scaled (builtScale));
    }
    base.assign ((size_t) pw * (size_t) ph * 3, 0);
    {
        const juce::Image::BitmapData src (bg, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x)
            {
                const auto* b = reinterpret_cast<const juce::PixelARGB*> (src.getPixelPointer (x, y));
                const float* c = canvas.pixel (x, y);
                std::uint8_t* s = base.data() + 3 * ((size_t) y * (size_t) pw + (size_t) x);
                s[0] = std::max (b->getRed(), clip8 (c[0]));
                s[1] = std::max (b->getGreen(), clip8 (c[1]));
                s[2] = std::max (b->getBlue(), clip8 (c[2]));
            }
    }
    still = base;
    frame = juce::Image (juce::Image::ARGB, pw, ph, false);
    drawnBoxes.clear();
    selItem = -1;
    selBox = {};
    restore ({ 0, 0, pw, ph });
    if (mode == Mode::kit)
    {
        applySelection();
        drawPedals ({ juce::Rectangle<int> (0, 0, pw, ph) }, juce::Time::getMillisecondCounterHiRes());
    }

    // hits that came before the kit was built: find their drums now
    for (int i = 0; i < numHits; ++i)
        if (hits[(size_t) i].item <= -2)
        {
            const int k = -2 - hits[(size_t) i].item;
            hits[(size_t) i].item = -1;
            for (size_t j = 0; j < items.size(); ++j)
                if (items[j].kitIndex == k)
                {
                    hits[(size_t) i].item = (int) j;
                    hits[(size_t) i].angle += items[j].drawing.head.nearAngle();
                }
        }
    stillMs = juce::Time::getMillisecondCounterHiRes() - t0;
}

// kit: the selected drum drawn 35 % brighter over the base, behind the drums in front of it.
void WireView::applySelection()
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    juce::Rectangle<int> dirty = selBox;
    if (! selBox.isEmpty())
        for (int y = selBox.getY(); y < selBox.getBottom(); ++y)
        {
            const size_t o = 3 * ((size_t) y * (size_t) pw + (size_t) selBox.getX());
            std::copy (base.begin() + (std::ptrdiff_t) o, base.begin() + (std::ptrdiff_t) (o + 3 * (size_t) selBox.getWidth()),
                       still.begin() + (std::ptrdiff_t) o);
        }
    selItem = -1;
    selBox = {};
    const int k = wire::kitItemForDrum (shownDrum);
    for (size_t i = 0; i < items.size(); ++i)
        if (k >= 0 && items[i].kitIndex == k)
            selItem = (int) i;
    if (selItem >= 0)
    {
        const auto& kit = wire::kitLayout()[(size_t) k];
        selDrawing = wire::drawDrum (wire::kitSpec (kit, depthOff[(size_t) kit.drum]), wire::kitCamera(), 1.0, finish);
        if (kit.spec.kind == wire::Kind::kick)
            wire::addKickPedal (selDrawing, kit.spec, wire::kitCamera(), 1.0, 0.0, wire::PedalParts::fixed);
        selBox = pixelBox (selDrawing.x0, selDrawing.y0, selDrawing.x1, selDrawing.y1, 14.0f);
        if (! selBox.isEmpty())
        {
            const auto keep = keepFor ((size_t) selItem, selBox);
            boxCanvas (selBox);
            drawDrumLines (selDrawing, true);
            canvas.bloom();
            for (int y = 0; y < selBox.getHeight(); ++y)
                for (int x = 0; x < selBox.getWidth(); ++x)
                {
                    const float* c = canvas.pixel (x, y);
                    const float kk = keep[(size_t) y * (size_t) selBox.getWidth() + (size_t) x];
                    std::uint8_t* s = still.data() + 3 * ((size_t) (y + selBox.getY()) * (size_t) pw + (size_t) (x + selBox.getX()));
                    s[0] = std::max (s[0], clip8 (c[0] * kk));
                    s[1] = std::max (s[1], clip8 (c[1] * kk));
                    s[2] = std::max (s[2], clip8 (c[2] * kk));
                }
        }
    }
    dirty = dirty.getUnion (selBox);
    restore (dirty);
    for (const auto& b : drawnBoxes) // the moving heads there are drawn again with the next frame
        restore (b);
    {
        std::vector<juce::Rectangle<int>> restored (drawnBoxes);
        restored.push_back (dirty);
        drawPedals (restored, juce::Time::getMillisecondCounterHiRes());
    }
    drawnBoxes.clear();
    if (numHits > 0)
        drawFrame (juce::Time::getMillisecondCounterHiRes());
    repaint ((dirty.toFloat() / builtScale).getSmallestIntegerContainer().expanded (1));
    stillMs = juce::Time::getMillisecondCounterHiRes() - t0;
}

void WireView::restore (juce::Rectangle<int> b)
{
    b = b.getIntersection ({ 0, 0, pw, ph });
    if (b.isEmpty())
        return;
    juce::Image::BitmapData dst (frame, b.getX(), b.getY(), b.getWidth(), b.getHeight(), juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < b.getHeight(); ++y)
    {
        const std::uint8_t* s = still.data() + 3 * ((size_t) (y + b.getY()) * (size_t) pw + (size_t) b.getX());
        auto* p = dst.getLinePointer (y);
        for (int x = 0; x < b.getWidth(); ++x, s += 3, p += dst.pixelStride)
            reinterpret_cast<juce::PixelARGB*> (p)->setARGB (255, s[0], s[1], s[2]);
    }
}

juce::Rectangle<int> WireView::overlay (size_t i, const ActiveHit* active, int count, double nowMs, bool fromStill)
{
    Item& it = items[i];
    if (it.box.isEmpty())
        return {};
    std::array<wire::Hit, 16> hs {};
    for (int k = 0; k < count; ++k)
        hs[(size_t) k] = { (nowMs - active[k].startMs) * 0.001, active[k].vel, active[k].rpos, active[k].angle };
    segs.clear();
    flashes.clear();
    const auto& head = (int) i == selItem ? selDrawing.head : it.drawing.head;
    head.draw (hs.data(), count, segs, flashes);

    // the box for this frame: the head as far as it can flex, the bloom, the flashes
    auto b = pixelBox (head.x0, head.y0, head.x1, head.y1, 2.0f * head.flexReach + 12.0f);
    for (const auto& f : flashes)
        b = b.getUnion (pixelBox (f.centre.x, f.centre.y, f.centre.x, f.centre.y, 2.8f * f.radius + 2.0f));
    b = b.getIntersection (it.box);
    if (b.isEmpty())
        return {};
    if (mode == Mode::kit && ! it.keepReady)
    {
        it.keep = keepFor (i, it.box);
        it.keepReady = true;
    }
    boxCanvas (b);
    canvas.draw (segs, false);
    for (const auto& f : flashes)
        canvas.addFlash (f.centre, f.radius, f.strength);
    canvas.bloom();
    const bool occluded = mode == Mode::kit && ! it.keep.empty();
    juce::Image::BitmapData dst (frame, b.getX(), b.getY(), b.getWidth(), b.getHeight(), juce::Image::BitmapData::readWrite);
    for (int y = 0; y < b.getHeight(); ++y)
    {
        auto* p = dst.getLinePointer (y);
        const float* keepRow = occluded ? it.keep.data() + (size_t) (y + b.getY() - it.box.getY()) * (size_t) it.box.getWidth()
                                              + (size_t) (b.getX() - it.box.getX())
                                        : nullptr;
        const float* c = canvas.pixel (0, y);
        if (fromStill)
        {
            // the still drawing and the head in one pass (one moving head: the DRUM page)
            const std::uint8_t* s = still.data() + 3 * ((size_t) (y + b.getY()) * (size_t) pw + (size_t) b.getX());
            for (int x = 0; x < b.getWidth(); ++x, p += dst.pixelStride, c += 3, s += 3)
                reinterpret_cast<juce::PixelARGB*> (p)->setARGB (255, std::max (s[0], clip8 (c[0])), std::max (s[1], clip8 (c[1])),
                                                                 std::max (s[2], clip8 (c[2])));
            continue;
        }
        for (int x = 0; x < b.getWidth(); ++x, p += dst.pixelStride, c += 3)
        {
            if (c[0] < 0.5f && c[1] < 0.5f && c[2] < 0.5f)
                continue;
            const float k = keepRow != nullptr ? keepRow[x] : 1.0f;
            auto* q = reinterpret_cast<juce::PixelARGB*> (p);
            q->setARGB (255, std::max (q->getRed(), clip8 (c[0] * k)), std::max (q->getGreen(), clip8 (c[1] * k)),
                        std::max (q->getBlue(), clip8 (c[2] * k)));
        }
    }
    return b;
}

void WireView::drawFrame (double nowMs)
{
    if (! frame.isValid())
        return;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    // hits that have stopped moving go
    int n = 0;
    for (int i = 0; i < numHits; ++i)
        if (nowMs - hits[(size_t) i].startMs < kMoveMs && hits[(size_t) i].item >= 0 && hits[(size_t) i].item < (int) items.size())
            hits[(size_t) n++] = hits[(size_t) i];
    numHits = n;

    std::vector<juce::Rectangle<int>> boxes;
    std::array<ActiveHit, 16> mine {};
    if (mode == Mode::drum)
    {
        // one head, always drawn: over the still drawing in its box, and the rest of the last
        // frame's box back to the still drawing
        for (int k = 0; k < numHits; ++k)
            mine[(size_t) k] = hits[(size_t) k];
        const auto b = items.empty() ? juce::Rectangle<int>() : overlay (0, mine.data(), numHits, nowMs, true);
        for (const auto& old : drawnBoxes)
        {
            juce::RectangleList<int> rest (old);
            rest.subtract (b);
            for (const auto& r : rest)
                restore (r);
        }
        juce::Rectangle<int> dirty = b;
        for (const auto& old : drawnBoxes)
            dirty = dirty.getUnion (old);
        drawnBoxes.clear();
        if (! b.isEmpty())
            drawnBoxes.push_back (b);
        if (! dirty.isEmpty())
            repaint ((dirty.toFloat() / builtScale).getSmallestIntegerContainer().expanded (1));
        frameMs = juce::Time::getMillisecondCounterHiRes() - t0;
        return;
    }
    // the pedals: a swinging one's box (or one that swung last frame) back to the still drawing,
    // then every pedal over what was restored, then the heads
    std::vector<juce::Rectangle<int>> restored (drawnBoxes);
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].pedal && (pedalSwingOf (i, nowMs) > 0.0 || items[i].pedalDrawn > 0.0))
            restored.push_back (items[i].pedalBox);
    for (const auto& b : restored)
        restore (b);
    drawPedals (restored, nowMs);
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].pedal && items[i].pedalDrawn > 0.0)
            boxes.push_back (items[i].pedalBox);
    for (size_t i = 0; i < items.size(); ++i)
    {
        int count = 0;
        for (int k = 0; k < numHits; ++k)
            if (hits[(size_t) k].item == (int) i)
                mine[(size_t) count++] = hits[(size_t) k];
        if (count == 0)
            continue;
        if (const auto b = overlay (i, mine.data(), count, nowMs, false); ! b.isEmpty())
            boxes.push_back (b);
    }
    juce::Rectangle<int> dirty;
    for (const auto& b : restored)
        dirty = dirty.getUnion (b);
    for (const auto& b : boxes)
        dirty = dirty.getUnion (b);
    drawnBoxes = std::move (boxes);
    if (! dirty.isEmpty())
        repaint ((dirty.toFloat() / builtScale).getSmallestIntegerContainer().expanded (1));
    frameMs = juce::Time::getMillisecondCounterHiRes() - t0;
}

// ---------------------------------------------------------------------------------------------
double WireView::pedalSwingOf (size_t i, double nowMs) const
{
    double s = 0.0;
    for (int k = 0; k < numHits; ++k)
        if (hits[(size_t) k].item == (int) i)
            s = std::max (s, wire::pedalSwing ((nowMs - hits[(size_t) k].startMs) * 0.001));
    return s;
}

void WireView::drawPedal (size_t i, double swing)
{
    Item& it = items[i];
    const auto b = it.pedalBox;
    if (b.isEmpty())
        return;
    if (! it.pedalKeepReady)
    {
        it.pedalKeep = keepFor (i, b);
        it.pedalKeepReady = true;
    }
    wire::DrumDrawing parts;
    wire::addKickPedal (parts, it.pedalKick, wire::kitCamera(), (int) i == selItem ? 1.0 : 0.0, swing, wire::PedalParts::moving);
    boxCanvas (b);
    canvas.draw (parts.segments, false);
    canvas.bloom();
    juce::Image::BitmapData dst (frame, b.getX(), b.getY(), b.getWidth(), b.getHeight(), juce::Image::BitmapData::readWrite);
    for (int y = 0; y < b.getHeight(); ++y)
    {
        auto* p = dst.getLinePointer (y);
        const float* keepRow = it.pedalKeep.data() + (size_t) y * (size_t) b.getWidth();
        const float* c = canvas.pixel (0, y);
        for (int x = 0; x < b.getWidth(); ++x, p += dst.pixelStride, c += 3)
        {
            if (c[0] < 0.5f && c[1] < 0.5f && c[2] < 0.5f)
                continue;
            auto* q = reinterpret_cast<juce::PixelARGB*> (p);
            const float k = keepRow[x];
            q->setARGB (255, std::max (q->getRed(), clip8 (c[0] * k)), std::max (q->getGreen(), clip8 (c[1] * k)),
                        std::max (q->getBlue(), clip8 (c[2] * k)));
        }
    }
    it.pedalDrawn = swing;
}

void WireView::drawPedals (const std::vector<juce::Rectangle<int>>& restored, double nowMs)
{
    if (mode != Mode::kit || ! frame.isValid())
        return;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (! items[i].pedal)
            continue;
        const double s = pedalSwingOf (i, nowMs);
        bool touched = s > 0.0 || items[i].pedalDrawn > 0.0;
        for (const auto& b : restored)
            touched = touched || b.intersects (items[i].pedalBox);
        if (touched)
            drawPedal (i, s);
    }
}

// ---------------------------------------------------------------------------------------------
int WireView::itemAt (juce::Point<float> p) const
{
    for (size_t i = items.size(); i-- > 0;) // near first
        if (insideConvex (items[i].drawing.silhouette, p))
            return (int) i;
    return -1;
}

void WireView::mouseMove (const juce::MouseEvent& e)
{
    if (mode != Mode::kit)
        return;
    const bool over = itemAt (e.position + designArea.getPosition().toFloat()) >= 0;
    setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void WireView::mouseUp (const juce::MouseEvent& e)
{
    if (mode != Mode::kit || ! e.mouseWasClicked())
        return;
    const int i = itemAt (e.position + designArea.getPosition().toFloat());
    if (i < 0 || ! onDrumClicked)
        return;
    onDrumClicked (items[(size_t) i].drum);
}

} // namespace tympanis::ui
