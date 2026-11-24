#pragma once

// A small software rasteriser for the wireframe drums: anti-aliased lines combined by their
// brightest value (the mockup's np.maximum of OpenCV lines), convex polygon masks, the mockup's
// bloom, and the flash spots. Plain C++, no JUCE. Float RGB, values may exceed 255.

#include "DrumModel.h"

#include <vector>

namespace tympanis::ui::wire
{

class Canvas
{
public:
    // A canvas of w x h pixels showing the design-unit rectangle from (originX, originY) at
    // `scale` pixels per design unit.
    void reset (int w, int h, float originX, float originY, float scale);
    void clear();
    void fill (float value); // every channel of every pixel

    int width() const noexcept { return w; }
    int height() const noexcept { return h; }
    float scale() const noexcept { return s; }
    float* pixel (int x, int y) noexcept { return rgb.data() + 3 * ((size_t) y * (size_t) w + (size_t) x); }
    const float* pixel (int x, int y) const noexcept { return rgb.data() + 3 * ((size_t) y * (size_t) w + (size_t) x); }

    // A segment, brightest-wins. `keep`, if given: a mask (w x h) whose value v scales the line
    // by (1 - v) (the side layer outside the body hull).
    void draw (const Segment& sgm, const float* keep = nullptr);
    void draw (const std::vector<Segment>& sgms, bool sideLayer, const float* keep = nullptr);

    // The coverage (0..1) of a convex polygon (design units) into `mask` (w x h floats, zero
    // where the polygon is not), as the larger of what is there and the polygon's; and back to
    // zero where it was.
    void fillMask (const std::vector<Pt>& poly, std::vector<float>& mask) const;
    void clearMask (const std::vector<Pt>& poly, std::vector<float>& mask) const;
    // Multiplies `keep` (w x h, 1 where nothing hides) by (1 - k x coverage) of the polygon.
    void occlude (const std::vector<Pt>& poly, float k, std::vector<float>& keep) const;
    // Multiplies every pixel the polygon covers by (1 - k x coverage): what a drum hides.
    void darken (const std::vector<Pt>& poly, float k);
    // The bloom of the mockup: where a pixel's brightest channel is over 140 it gains half the
    // canvas blurred with a gaussian of `sigma` design units. Nothing to do (and no time spent)
    // when no pixel is over 140.
    void bloom (float sigma = 3.0f);
    // The flash: a disc of `radius` (design units) in (255, 245, 230), blurred with a gaussian
    // of 0.6 x its radius, times `strength`, added.
    void addFlash (Pt centre, float radius, float strength);

private:
    template <typename Cover>
    void scanPolygon (const std::vector<Pt>& poly, Cover&& cover) const;

    int w = 0, h = 0;
    float ox = 0, oy = 0, s = 1;
    std::vector<float> rgb;
    std::vector<float> scratch;
};

} // namespace tympanis::ui::wire
