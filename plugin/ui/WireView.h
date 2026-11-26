#pragma once

// The wireframe drums on the editor's pages (docs/UI.md): drawn in software (DrumModel,
// WireRaster) into an image the size of the view in physical pixels, then shown 1:1. The
// still part (shells, hardware, heads at rest, the bloom, over the background) is drawn once per
// drum, selection or size; after a hit only the struck head is drawn again, in its own box, at
// 30 Hz while it moves (2 s), then the timer stops. The message thread does all of it.
//
//   drum:  the DRUM page's drum, in the area above the bottom row and left of the right column
//   small: the TUNING page's small drum, in the left column
//   kit:   the KIT page: the kit; a click on a drum selects it

#include "DrumModel.h"
#include "UiAssets.h"
#include "WireRaster.h"

#include <array>
#include <functional>
#include <vector>

namespace tympanis::ui
{

class WireView final : public juce::Component, private juce::Timer
{
public:
    enum class Mode
    {
        drum,
        small,
        kit
    };
    WireView (const Assets& assets, Mode mode);
    ~WireView() override;

    static juce::Rectangle<int> area (Mode mode);

    // drum, small: the drum shown; kit: the selected drum (35 % brighter). KitMap.h index.
    void setDrum (int drum);
    int drum() const noexcept { return shownDrum; }

    // Each drum's shell, inches deeper than drawn by default (Size on Toms 1-4; KitMap.h order).
    // A change redraws the still drawing (kit: the whole kit; drum, small: if the drum shown).
    void setDepths (const std::array<double, wire::kDrumCount>& offsetsIn);

    // A strike on a drum: its head moves and flashes (drum: only if it is the drum shown; small:
    // never). rpos: 0 centre .. 1 edge. ageSeconds: how long ago it struck (tools).
    void strike (int drum, float velocity, double rpos, double ageSeconds = 0.0);

    // kit: a click on a drum (KitMap.h index).
    std::function<void (int drum)> onDrumClicked;

    // For tools/tympanis_snapshot: draws the current frame now, at this physical scale; the time
    // the last still drawing and the last moving frame took.
    void renderNow (float scale);
    double lastStillMs() const noexcept { return stillMs; }
    double lastFrameMs() const noexcept { return frameMs; }
    int activeHits() const noexcept { return numHits; }

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    struct Item
    {
        int drum = -1;
        int kitIndex = -1;         // kit: index into wire::kitLayout()
        wire::DrumDrawing drawing;
        juce::Rectangle<int> box;  // physical pixels in the view: where its moving head can be drawn
        bool keepReady = false;
        std::vector<float> keep;   // kit: what the drums in front leave of it, over `box`
        // kit, the kicks: the pedal's moving parts (the still drawing has its fixed parts), drawn
        // over the frame in their own box, at rest or swinging after a hit
        bool pedal = false;
        wire::DrumSpec pedalKick {};
        juce::Rectangle<int> pedalBox;
        bool pedalKeepReady = false;
        std::vector<float> pedalKeep;
        double pedalDrawn = 0.0;   // the swing last drawn
    };
    struct ActiveHit
    {
        int item;
        double startMs;
        double vel, rpos, angle;
    };

    void timerCallback() override;
    void build (float scale);
    void applySelection();
    void drawFrame (double nowMs);
    juce::Rectangle<int> pixelBox (float x0, float y0, float x1, float y1, float margin) const;
    void boxCanvas (juce::Rectangle<int> box);
    std::vector<float> keepFor (size_t itemIndex, juce::Rectangle<int> box);
    void drawDrumLines (const wire::DrumDrawing& dr, bool withHead);
    void restore (juce::Rectangle<int> box);
    // Draws an item's moving head into the frame (over what is there, or over the still drawing);
    // returns the box it drew in.
    juce::Rectangle<int> overlay (size_t itemIndex, const ActiveHit* hits, int count, double nowMs, bool fromStill);
    int itemAt (juce::Point<float> designPoint) const;
    // kit: the pedals' moving parts over the frame: those swinging, and the others where a box
    // was just restored from the still drawing (drawing one again at the same swing changes nothing)
    double pedalSwingOf (size_t itemIndex, double nowMs) const;
    void drawPedal (size_t itemIndex, double swing);
    void drawPedals (const std::vector<juce::Rectangle<int>>& restored, double nowMs);

    const Assets& assets;
    const Mode mode;
    const juce::Rectangle<int> designArea;
    int shownDrum = 8;
    std::array<double, wire::kDrumCount> depthOff {};

    float builtScale = 0.0f;
    bool needsBuild = true;
    std::vector<Item> items;           // kit: far first
    std::vector<std::uint8_t> base;    // RGB: the still drawing over the background
    std::vector<std::uint8_t> still;   // base with the selected kit drum brighter
    juce::Image frame;                 // still with the moving heads: what paint() shows
    int pw = 0, ph = 0;                // their size in pixels
    wire::Canvas canvas;
    std::vector<float> mask;
    std::vector<wire::Segment> segs;
    std::vector<wire::HeadMesh::Flash> flashes;

    // kit: the selected drum, drawn 35 % brighter over the base
    int selItem = -1;
    wire::DrumDrawing selDrawing;
    juce::Rectangle<int> selBox;

    std::array<ActiveHit, 16> hits {};
    int numHits = 0;
    std::vector<juce::Rectangle<int>> drawnBoxes; // moving heads drawn in the last frame
    juce::Random random;
    double stillMs = 0.0, frameMs = 0.0;

    static constexpr double kMoveMs = 2000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WireView)
};

} // namespace tympanis::ui
