#pragma once

// Drawing resources and helpers for the editor (from Cymbalis, docs/UI_SPEC.md there; Tympanis:
// docs/UI.md).

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace tympanis::ui
{

// All coordinates are design units of the 1536 x 1024 artwork.
constexpr int kDesignWidth = 1536;
constexpr int kDesignHeight = 1024;

// Palette, Cymbalis's (sampled from its background artwork).
namespace palette
{
const juce::Colour background { 0xff0f0e0f };
const juce::Colour label { 0xffa7a7a4 };       // main labels, headers, values
const juce::Colour knobLabel { 0xffa09c99 };   // Velocity Sensitivity, Volume
const juce::Colour secondary { 0xff695e52 };   // row labels, knob end labels, secondary text
const juce::Colour bronze { 0xff76624c };      // accent; hover outline
const juce::Colour boxOutline { 0xff534739 };  // preset and channel boxes; popup border
const juce::Colour rowOutline { 0xff463c30 };  // row boxes
const juce::Colour arrow { 0xff999489 };       // dropdown arrow
const juce::Colour divider { 0xff353231 };
const juce::Colour popup { 0xff141313 };       // popup background
const juce::Colour openOutline { 0xffa5a299 }; // outline of a box whose popup is open; a lit row's label
// The knobs' position arcs: the track, and the lit part from dark bronze
// to bright gold.
const juce::Colour arcTrack { 0xff1c1a18 };
const juce::Colour arcDark { 0xff5a4a36 };
const juce::Colour arcBright { 0xffecc88c };
// The knob, drawn by the plugin (as Cymbalis since 2026-11-24, its UI_SPEC section 6, "The
// knob"), lit from the top left: its body's gradient, light to dark; its rim's gradient, at 0,
// 0.06, 0.14, 0.5, 0.8 and 1 along the same line; its pointer, and the pointer's shadow.
const juce::Colour knobBodyLight { 0xff555555 };
const juce::Colour knobBodyDark { 0xff191919 };
const juce::Colour knobRim[] { juce::Colour (0xff969696), juce::Colour (0xff606060), juce::Colour (0xff3c3c3c),
                               juce::Colour (0xff1a1a1a), juce::Colour (0xff060606), juce::Colour (0xff000000) };
const juce::Colour knobPointer { 0xffb8b8b8 };
const juce::Colour knobPointerShadow { (juce::uint8) 0, (juce::uint8) 0, (juce::uint8) 0, 0.48f };
// The DHammers logo: a vertical gradient over its ink.
const juce::Colour logoTop { 0xff8e7550 };
const juce::Colour logoBottom { 0xff4a3d2d };
// A lit row: its label is openOutline, its note box bronze. The wireframe drums' colours are in
// DrumModel.cpp.
} // namespace palette

// The images and the typeface, loaded once from BinaryData.
class Assets
{
public:
    Assets();

    juce::Image background; // TYMPANIS_Background.png, title included
    juce::Typeface::Ptr typeface;

    // Em size per unit of JUCE font height for this typeface. JUCE's "points" are the em
    // size with CoreText but the ascent with FreeType, so the editor measures it: the ink
    // height of "H" against the font's cap height (OS/2 table).
    float emPerHeight = 2048.0f / 2500.0f;

    // The UI font at `sizePx` (em size, design units) with `spacingPx` of letter spacing.
    juce::Font font (float sizePx, float spacingPx = 0.0f) const;

    // Pinyon Script, for the DHammers logo (PinyonScript-Regular.ttf), and its em size per
    // unit of JUCE font height: unitsPerEm / (hhea ascender - descender), the height JUCE
    // gives a typeface with CoreText and FreeType alike.
    juce::Typeface::Ptr logoTypeface;
    float logoEmPerHeight = 2048.0f / 2555.0f;

    // The logo font at `sizePx` (em size, design units).
    juce::Font logoFont (float sizePx) const;

};

// Tight ink bounds of `text` set on a baseline at y = 0.
juce::Rectangle<float> inkBounds (const juce::Font& font, const juce::String& text);

// Draws `text` so that its ink starts at `inkTopLeft`.
void drawInkText (juce::Graphics& g, const juce::Font& font, const juce::String& text, juce::Point<float> inkTopLeft,
                  juce::Colour colour);

// Draws `text` with its ink centred on `centre`.
void drawInkTextCentred (juce::Graphics& g, const juce::Font& font, const juce::String& text,
                         juce::Point<float> centre, juce::Colour colour);

// "C3 60" style note naming (Logic/Ableton: 60 = C3). Empty name for -1 (unassigned).
juce::String noteName (int note);

} // namespace tympanis::ui
