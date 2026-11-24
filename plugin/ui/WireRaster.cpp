#include "WireRaster.h"

#include <algorithm>
#include <cmath>

namespace tympanis::ui::wire
{

void Canvas::reset (int width, int height, float originX, float originY, float scale)
{
    w = std::max (1, width);
    h = std::max (1, height);
    ox = originX;
    oy = originY;
    s = scale;
    rgb.assign ((size_t) w * (size_t) h * 3, 0.0f);
}

void Canvas::clear()
{
    std::fill (rgb.begin(), rgb.end(), 0.0f);
}

void Canvas::fill (float value)
{
    std::fill (rgb.begin(), rgb.end(), value);
}

void Canvas::draw (const Segment& sgm, const float* keep)
{
    const float ax = (sgm.a.x - ox) * s, ay = (sgm.a.y - oy) * s;
    const float bx = (sgm.b.x - ox) * s, by = (sgm.b.y - oy) * s;
    const float wpx = sgm.width * s;
    const float half = std::max (0.5f, 0.5f * wpx);
    const float k = std::min (1.0f, wpx); // a line thinner than a pixel is dimmer
    const float reach = half + 0.5f; // coverage is zero beyond
    const int x0 = std::max (0, (int) std::floor (std::min (ax, bx) - reach));
    const int x1 = std::min (w - 1, (int) std::ceil (std::max (ax, bx) + reach));
    const int y0 = std::max (0, (int) std::floor (std::min (ay, by) - reach));
    const int y1 = std::min (h - 1, (int) std::ceil (std::max (ay, by) + reach));
    const float reach2 = reach * reach;
    if (x0 > x1 || y0 > y1)
        return;
    const float dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    const float inv = len2 > 1e-9f ? 1.0f / len2 : 0.0f;
    const float cr = sgm.c.r * k, cg = sgm.c.g * k, cb = sgm.c.b * k;
    for (int y = y0; y <= y1; ++y)
    {
        const float py = (float) y + 0.5f - ay;
        float* row = pixel (0, y);
        const float* keepRow = keep != nullptr ? keep + (size_t) y * (size_t) w : nullptr;
        for (int x = x0; x <= x1; ++x)
        {
            const float px = (float) x + 0.5f - ax;
            const float t = std::clamp ((px * dx + py * dy) * inv, 0.0f, 1.0f);
            const float ex = px - t * dx, ey = py - t * dy;
            const float d2 = ex * ex + ey * ey;
            if (d2 >= reach2)
                continue;
            const float cov = std::min (reach - std::sqrt (d2), 1.0f);
            const float k2 = keepRow != nullptr ? cov * (1.0f - keepRow[x]) : cov;
            float* p = row + 3 * x;
            p[0] = std::max (p[0], cr * k2);
            p[1] = std::max (p[1], cg * k2);
            p[2] = std::max (p[2], cb * k2);
        }
    }
}

void Canvas::draw (const std::vector<Segment>& sgms, bool sideLayer, const float* keep)
{
    for (const auto& sgm : sgms)
        if (sgm.side == sideLayer)
            draw (sgm, sideLayer ? keep : nullptr);
}

// Calls cover (x, y, coverage) for each pixel the convex polygon covers (4 sub-rows a pixel,
// exact along each sub-row).
template <typename Cover>
void Canvas::scanPolygon (const std::vector<Pt>& poly, Cover&& cover) const
{
    if (poly.size() < 3)
        return;
    std::vector<Pt> p (poly.size());
    float minY = 1e9f, maxY = -1e9f;
    for (size_t i = 0; i < poly.size(); ++i)
    {
        p[i] = { (poly[i].x - ox) * s, (poly[i].y - oy) * s };
        minY = std::min (minY, p[i].y);
        maxY = std::max (maxY, p[i].y);
    }
    const int y0 = std::max (0, (int) std::floor (minY)), y1 = std::min (h - 1, (int) std::ceil (maxY));
    std::vector<float> acc ((size_t) w + 2);
    for (int y = y0; y <= y1; ++y)
    {
        int lo = w, hi = -1;
        for (int sub = 0; sub < 4; ++sub)
        {
            const float Y = (float) y + ((float) sub + 0.5f) * 0.25f;
            float xl = 1e9f, xr = -1e9f;
            for (size_t i = 0; i < p.size(); ++i)
            {
                const Pt a = p[i], b = p[(i + 1) % p.size()];
                if ((a.y <= Y && b.y > Y) || (b.y <= Y && a.y > Y))
                {
                    const float x = a.x + (Y - a.y) / (b.y - a.y) * (b.x - a.x);
                    xl = std::min (xl, x);
                    xr = std::max (xr, x);
                }
            }
            if (xr < xl)
                continue;
            const int xa = std::max (0, (int) std::floor (xl)), xb = std::min (w - 1, (int) std::floor (xr));
            for (int x = xa; x <= xb; ++x)
            {
                const float c = std::min ((float) x + 1.0f, xr) - std::max ((float) x, xl);
                if (c > 0.0f)
                    acc[(size_t) x] += c * 0.25f;
            }
            lo = std::min (lo, xa);
            hi = std::max (hi, xb);
        }
        for (int x = lo; x <= hi; ++x)
        {
            if (acc[(size_t) x] > 0.0f)
                cover (x, y, std::min (1.0f, acc[(size_t) x]));
            acc[(size_t) x] = 0.0f;
        }
    }
}

void Canvas::fillMask (const std::vector<Pt>& poly, std::vector<float>& mask) const
{
    mask.resize ((size_t) w * (size_t) h, 0.0f);
    scanPolygon (poly, [&] (int x, int y, float c) {
        float& m = mask[(size_t) y * (size_t) w + (size_t) x];
        m = std::max (m, c);
    });
}

void Canvas::clearMask (const std::vector<Pt>& poly, std::vector<float>& mask) const
{
    scanPolygon (poly, [&] (int x, int y, float) { mask[(size_t) y * (size_t) w + (size_t) x] = 0.0f; });
}

void Canvas::occlude (const std::vector<Pt>& poly, float k, std::vector<float>& keep) const
{
    scanPolygon (poly, [&] (int x, int y, float c) { keep[(size_t) y * (size_t) w + (size_t) x] *= 1.0f - k * c; });
}

void Canvas::darken (const std::vector<Pt>& poly, float k)
{
    scanPolygon (poly, [&] (int x, int y, float c) {
        float* p = pixel (x, y);
        const float f = 1.0f - k * c;
        p[0] *= f;
        p[1] *= f;
        p[2] *= f;
    });
}

namespace
{
// One pass of a box blur of radius r along a line of n values with stride `step` (zero outside).
void boxLine (float* data, int n, size_t step, int r, std::vector<float>& tmp)
{
    tmp.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
        tmp[(size_t) i] = data[(size_t) i * step];
    const float inv = 1.0f / (float) (2 * r + 1);
    float sum = 0.0f;
    for (int i = 0; i <= std::min (r, n - 1); ++i)
        sum += tmp[(size_t) i];
    for (int i = 0; i < n; ++i)
    {
        data[(size_t) i * step] = sum * inv;
        const int add = i + r + 1, sub = i - r;
        if (add < n)
            sum += tmp[(size_t) add];
        if (sub >= 0)
            sum -= tmp[(size_t) sub];
    }
}
} // namespace

void Canvas::bloom (float sigma)
{
    // blurred at about half a design unit per pixel (the blur is wide: no detail is lost)
    const int f = std::max (1, (int) std::lround (2.0f * s));
    const int bw = (w + f - 1) / f, bh = (h + f - 1) / f;
    scratch.assign ((size_t) bw * (size_t) bh * 3, 0.0f);
    bool bright = false;
    for (int y = 0; y < h; ++y)
    {
        const float* p = pixel (0, y);
        float* q = scratch.data() + 3 * (size_t) (y / f) * (size_t) bw;
        for (int bx = 0, x = 0; bx < bw; ++bx, q += 3)
            for (int k = 0; k < f && x < w; ++k, ++x, p += 3)
            {
                q[0] += p[0];
                q[1] += p[1];
                q[2] += p[2];
                bright = bright || p[0] > 140.0f || p[1] > 140.0f || p[2] > 140.0f;
            }
    }
    if (! bright)
        return;
    if (f > 1)
        for (float& v : scratch)
            v *= 1.0f / (float) (f * f);
    const float sg = sigma * s / (float) f;
    const int r = std::max (1, (int) std::lround ((std::sqrt (4.0f * sg * sg + 1.0f) - 1.0f) * 0.5f));
    std::vector<float> tmp;
    for (int pass = 0; pass < 3; ++pass)
    {
        for (int y = 0; y < bh; ++y)
            for (int c = 0; c < 3; ++c)
                boxLine (scratch.data() + 3 * (size_t) y * (size_t) bw + (size_t) c, bw, 3, r, tmp);
        for (int x = 0; x < bw; ++x)
            for (int c = 0; c < 3; ++c)
                boxLine (scratch.data() + 3 * (size_t) x + (size_t) c, bh, 3 * (size_t) bw, r, tmp);
    }
    for (int y = 0; y < h; ++y)
    {
        float* p = pixel (0, y);
        for (int x = 0; x < w; ++x, p += 3)
        {
            if (p[0] <= 140.0f && p[1] <= 140.0f && p[2] <= 140.0f)
                continue;
            float b[3];
            if (f == 1)
            {
                const float* q = scratch.data() + 3 * ((size_t) y * (size_t) bw + (size_t) x);
                b[0] = q[0], b[1] = q[1], b[2] = q[2];
            }
            else
            {
                // bilinear, at the pixel's centre in the coarse grid
                const float fx = std::clamp (((float) x + 0.5f) / (float) f - 0.5f, 0.0f, (float) (bw - 1));
                const float fy = std::clamp (((float) y + 0.5f) / (float) f - 0.5f, 0.0f, (float) (bh - 1));
                const int ix = (int) fx, iy = (int) fy;
                const int jx = std::min (ix + 1, bw - 1), jy = std::min (iy + 1, bh - 1);
                const float tx = fx - (float) ix, ty = fy - (float) iy;
                for (int c = 0; c < 3; ++c)
                {
                    auto at = [&] (int xx, int yy) { return scratch[3 * ((size_t) yy * (size_t) bw + (size_t) xx) + (size_t) c]; };
                    b[c] = (at (ix, iy) * (1 - tx) + at (jx, iy) * tx) * (1 - ty) + (at (ix, jy) * (1 - tx) + at (jx, jy) * tx) * ty;
                }
            }
            p[0] += 0.5f * b[0];
            p[1] += 0.5f * b[1];
            p[2] += 0.5f * b[2];
        }
    }
}

void Canvas::addFlash (Pt centre, float radius, float strength)
{
    const float cx = (centre.x - ox) * s, cy = (centre.y - oy) * s;
    const float R = radius * s, sg = 0.6f * R;
    if (R <= 0.0f || strength <= 0.0f)
        return;
    const float reach = R + 3.0f * sg;
    const int x0 = std::max (0, (int) std::floor (cx - reach)), x1 = std::min (w - 1, (int) std::ceil (cx + reach));
    const int y0 = std::max (0, (int) std::floor (cy - reach)), y1 = std::min (h - 1, (int) std::ceil (cy + reach));
    // a disc blurred by a gaussian, approximately: 0.5 erfc ((d - R) / (sigma sqrt 2)), scaled to
    // the true value at the centre; tabulated by distance in quarter pixels
    const float centreValue = 1.0f - std::exp (-R * R / (2.0f * sg * sg));
    const float norm = centreValue / (0.5f * std::erfc (-R / (sg * 1.41421356f))) * strength;
    const int n = (int) std::ceil (reach * 4.0f) + 2;
    std::vector<float> table ((size_t) n);
    for (int i = 0; i < n; ++i)
        table[(size_t) i] = 0.5f * std::erfc (((float) i * 0.25f - R) / (sg * 1.41421356f)) * norm;
    const float reach2 = reach * reach;
    for (int y = y0; y <= y1; ++y)
    {
        const float dy = (float) y + 0.5f - cy;
        float* p = pixel (x0, y);
        for (int x = x0; x <= x1; ++x, p += 3)
        {
            const float dx = (float) x + 0.5f - cx;
            const float d2 = dx * dx + dy * dy;
            if (d2 > reach2)
                continue;
            const float v = table[(size_t) std::min (n - 1, (int) (std::sqrt (d2) * 4.0f + 0.5f))];
            p[0] += 255.0f * v;
            p[1] += 245.0f * v;
            p[2] += 230.0f * v;
        }
    }
}

} // namespace tympanis::ui::wire
