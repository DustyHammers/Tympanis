#include "DrumModel.h"

#include <memory>
#include <mutex>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tympanis::ui::wire
{

double dot (V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross (V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
double length (V3 a) { return std::sqrt (dot (a, a)); }
V3 normalised (V3 a) { return a * (1.0 / length (a)); }

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Colours: the mockup's BGR values as RGB.
// The shells' finishes (Kris, 2026-11-26): the body, where the light catches it, the edges.
struct Palette
{
    Rgb body, hi, dark;
};
constexpr Palette kGreen { { 64, 158, 80 }, { 128, 232, 150 }, { 30, 60, 28 } };     // British racing green lacquer
constexpr Palette kNatural { { 244, 214, 162 }, { 255, 246, 226 }, { 150, 112, 64 } }; // natural lacquered maple
const Palette& paletteOf (Finish f) { return f == Finish::natural ? kNatural : kGreen; }
constexpr Rgb kChrome { 220, 216, 214 };   // hoops, lugs, claws
constexpr Rgb kBrass { 214, 170, 84 };     // the air vents' grommets
constexpr Rgb kRubber { 96, 92, 90 };      // the throw-off lever's grip
constexpr Rgb kChromeD { 156, 152, 150 };
constexpr Rgb kHead { 222, 204, 178 };     // coated head, pale cream
constexpr Rgb kHot { 255, 238, 215 };

const V3 kLight = normalised ({ 0.2, -0.5, 0.85 });

// ---- The maple's grain on a shell (Kris's brochure photos, mockups approved 2026-11-26).
// Growth rings are levels of a field q on the shell (S around, H down from the batter edge,
// inches): level bands with gentle waves and lens-shaped "eyes" where the rings bulge. Each ring
// gets 3 lines, light in the early wood and darker into the late wood; the ring's edge is left
// black. The outer ply's two ends meet on a diagonal seam from the top of a top lug down to the
// bottom of the next bottom lug; the grain is laid out from the seam (u = S - seam) and does not
// line up across it: one cell of black, the cells either side 45 % bright.
struct GrainRng
{
    uint64_t x;
    double next()
    {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return (double) (x >> 11) * (1.0 / 9007199254740992.0);
    }
    double uni (double lo, double hi) { return lo + (hi - lo) * next(); }
};
struct Wave
{
    int kx;
    double ky, ph, amp;
};
std::vector<Wave> waves (GrainRng& g, int n, int kmax, double kyMax, double amp, double slope)
{
    std::vector<Wave> w;
    for (int i = 0; i < n; ++i)
    {
        const int kx = 1 + (int) (g.next() * kmax);
        w.push_back ({ kx, g.uni (-kyMax, kyMax), g.uni (0, 2 * kPi), amp * g.uni (0.4, 1.0) / std::pow ((double) kx, slope) });
    }
    return w;
}

// One line piece of the grain: its ends on the shell (cos and sin of the angle around, H inches
// down from the batter edge) and its brightness in the ring (the seam's fade included).
struct GrainSeg
{
    float c0, s0, h0, c1, s1, h1, k;
};

// What the grain depends on: the shell, where its lines start and stop (x0, x1: fractions of d),
// and the seam (the angles of its top and bottom ends, the heights of the lug ends it joins).
struct GrainKey
{
    int kind;
    double r, d, x0, x1, tTop, tBot, hTop, hBot;
    bool operator== (const GrainKey& o) const
    {
        return kind == o.kind && r == o.r && d == o.d && x0 == o.x0 && x1 == o.x1 && tTop == o.tTop && tBot == o.tBot && hTop == o.hTop
               && hBot == o.hBot;
    }
};

std::vector<GrainSeg> makeGrain (const GrainKey& key)
{
    // grain_rings.py preset q (the approved mockups), in inches on the shell
    const double r = key.r, d = key.d, x0 = key.x0, x1 = key.x1;
    const double ring = 0.9, early = 0.3, wave = 0.3;
    const int eyes = 7, nLines = 3;
    const double kb = 0.75; // the grain's brightness against the old mesh
    const double Ls = 2 * kPi * r;
    GrainRng g { (uint64_t) 0x9E3779B97F4A7C15u ^ (uint64_t) (r * 1000) * (uint64_t) 2654435761u ^ (uint64_t) (d * 100) * (uint64_t) 40503u ^ (uint64_t) key.kind };
    for (int i = 0; i < 8; ++i)
        g.next();
    auto w12 = waves (g, 8, 4, 0.12, wave, 1.1);
    const auto w2 = waves (g, 14, 12, 0.3, 0.2 * wave, 0.8);
    w12.insert (w12.end(), w2.begin(), w2.end());
    struct Eye
    {
        double s0, h0, len, hw, amp;
    };
    std::vector<Eye> ey;
    for (int e = 0; e < eyes; ++e)
        ey.push_back ({ (e + g.uni (0.25, 0.75)) * Ls / eyes, g.uni (0.2 * d, 0.8 * d), g.uni (2.5, 4.5), g.uni (1.0, 1.8),
                        (g.next() < 0.5 ? -1 : 1) * g.uni (1.6, 2.6) });
    const auto wq = waves (g, 6, 3, 0.3, 0.3, 1.0);
    const double drift = g.uni (0.35, 0.65) * ring;

    // the grid: u (from the seam) across the columns, H down the rows, a cell about 0.08" square
    const int NSg = std::max (64, (int) (Ls / 0.08)), NH = std::max (16, (int) ((x1 - x0) * d / 0.08));
    const size_t nc = (size_t) NSg + 1, nr = (size_t) NH + 1;
    std::vector<double> uC (nc), hR (nr), seamR (nr);
    for (size_t i = 0; i < nc; ++i)
        uC[i] = Ls * (double) i / NSg;
    for (size_t k = 0; k < nr; ++k)
    {
        hR[k] = d * (x0 + (x1 - x0) * (double) k / NH);
        seamR[k] = key.r * (key.tTop + (key.tBot - key.tTop) * (hR[k] - key.hTop) / (key.hBot - key.hTop));
    }
    // phi = H + the waves + the drift + the eyes. A wave's phase 2 pi kx S / Ls + ky H + ph with
    // S = seam(H) + u is a column part plus a row part, so each wave is cos A cos B - sin A sin B
    // from tables.
    std::vector<double> phi (nc * nr);
    for (size_t k = 0; k < nr; ++k)
        for (size_t i = 0; i < nc; ++i)
            phi[k * nc + i] = hR[k] + drift * uC[i] / Ls;
    {
        std::vector<double> cA (nc), sA (nc);
        for (const auto& w : w12)
        {
            for (size_t i = 0; i < nc; ++i)
            {
                const double A = 2 * kPi * w.kx * uC[i] / Ls;
                cA[i] = w.amp * std::cos (A);
                sA[i] = w.amp * std::sin (A);
            }
            for (size_t k = 0; k < nr; ++k)
            {
                const double B = 2 * kPi * w.kx * seamR[k] / Ls + w.ky * hR[k] + w.ph;
                const double cB = std::cos (B), sB = std::sin (B);
                double* row = &phi[k * nc];
                for (size_t i = 0; i < nc; ++i)
                    row[i] += cA[i] * cB - sA[i] * sB;
            }
        }
    }
    for (const auto& e : ey)
        for (size_t k = 0; k < nr; ++k)
        {
            const double across0 = 1 - std::abs (hR[k] - e.h0) / e.hw;
            if (across0 <= 0)
                continue;
            const double across = std::pow (across0, 1.2);
            for (size_t i = 0; i < nc; ++i)
            {
                const double ds = std::fmod (seamR[k] + uC[i] - e.s0 + 1.5 * Ls, Ls) - 0.5 * Ls;
                if (std::abs (ds) > 5 * e.len)
                    continue;
                phi[k * nc + i] += e.amp * std::exp (-(ds / e.len) * (ds / e.len)) * across;
            }
        }
    // q = phi / ring + slow waves along the rings themselves
    std::vector<double> q (nc * nr);
    for (size_t k = 0; k < nr; ++k)
        for (size_t i = 0; i < nc; ++i)
        {
            const double S = seamR[k] + uC[i], p = phi[k * nc + i];
            double z = p / ring;
            for (const auto& w : wq)
                z += w.amp * std::cos (2 * kPi * w.kx * S / Ls + w.ky * p + w.ph);
            q[k * nc + i] = z;
        }

    // the lines: places in each ring (0 at its start, 1 at its dark edge) and their brightness
    double mf[nLines], mk[nLines];
    for (int j = 0; j < nLines; ++j)
    {
        mf[j] = (j + 0.5) / nLines * 0.92; // the edge (f near 1) stays black
        const double late = std::clamp ((mf[j] - early) / (1 - early), 0.0, 1.0);
        mk[j] = kb * (1.0 - 0.62 * std::pow (late, 1.4)); // light wood 1, deep in the band 0.38
    }
    // marching squares; a point in grid units (fi, fk) to the shell
    std::vector<GrainSeg> out;
    out.reserve ((size_t) (nc * (d / ring) * nLines * 1.3));
    auto put = [&] (double fi, double fk, float& c, float& sn, float& h) {
        const double H = d * (x0 + (x1 - x0) * fk / NH);
        const double t = (key.r * (key.tTop + (key.tBot - key.tTop) * (H - key.hTop) / (key.hBot - key.hTop)) + Ls * fi / NSg) / r;
        c = (float) std::cos (t);
        sn = (float) std::sin (t);
        h = (float) H;
    };
    for (int i = 1; i < NSg; ++i) // column 0 is the seam: black
    {
        const double fade = i == 1 || i == NSg - 1 ? 0.45 : 1.0;
        for (int k = 0; k < NH; ++k)
        {
            const double c00 = q[(size_t) k * nc + (size_t) i], c10 = q[(size_t) k * nc + (size_t) i + 1];
            const double c01 = q[(size_t) (k + 1) * nc + (size_t) i], c11 = q[(size_t) (k + 1) * nc + (size_t) i + 1];
            const double lo = std::min ({ c00, c10, c01, c11 }), hi = std::max ({ c00, c10, c01, c11 });
            for (double ring0 = std::floor (lo); ring0 <= hi; ring0 += 1)
                for (int j = 0; j < nLines; ++j)
                {
                    const double L = ring0 + mf[j];
                    if (L < lo || L > hi)
                        continue;
                    // the cell's edges the level crosses: (i,k)-(i+1,k), (i+1,k)-(i+1,k+1), (i+1,k+1)-(i,k+1), (i,k+1)-(i,k)
                    const double cs[4][2][3] = { { { (double) i, (double) k, c00 }, { (double) i + 1, (double) k, c10 } },
                                                 { { (double) i + 1, (double) k, c10 }, { (double) i + 1, (double) k + 1, c11 } },
                                                 { { (double) i + 1, (double) k + 1, c11 }, { (double) i, (double) k + 1, c01 } },
                                                 { { (double) i, (double) k + 1, c01 }, { (double) i, (double) k, c00 } } };
                    double pts[4][2];
                    int np = 0;
                    for (const auto& e : cs)
                    {
                        const double a0 = e[0][2], a1 = e[1][2];
                        if ((a0 < L) == (a1 < L) || a0 == a1)
                            continue;
                        const double tt = (L - a0) / (a1 - a0);
                        pts[np][0] = e[0][0] + tt * (e[1][0] - e[0][0]);
                        pts[np][1] = e[0][1] + tt * (e[1][1] - e[0][1]);
                        ++np;
                    }
                    for (int pp = 0; pp + 1 < np; pp += 2)
                    {
                        GrainSeg sg;
                        put (pts[pp][0], pts[pp][1], sg.c0, sg.s0, sg.h0);
                        put (pts[pp + 1][0], pts[pp + 1][1], sg.c1, sg.s1, sg.h1);
                        sg.k = (float) (mk[j] * fade);
                        out.push_back (sg);
                    }
                }
        }
    }
    return out;
}

// The grain of the shells drawn lately (a drum's grain is worked out once per shell and seam,
// then only projected and lit): the newest last, at most kGrainCacheSize.
std::shared_ptr<const std::vector<GrainSeg>> grainFor (const GrainKey& key)
{
    constexpr size_t kGrainCacheSize = 24;
    static std::mutex lock;
    static std::vector<std::pair<GrainKey, std::shared_ptr<const std::vector<GrainSeg>>>> cache;
    {
        std::lock_guard<std::mutex> gl (lock);
        for (size_t i = 0; i < cache.size(); ++i)
            if (cache[i].first == key)
            {
                auto hit = cache[i];
                cache.erase (cache.begin() + (std::ptrdiff_t) i);
                cache.push_back (hit);
                return hit.second;
            }
    }
    auto made = std::make_shared<const std::vector<GrainSeg>> (makeGrain (key));
    std::lock_guard<std::mutex> gl (lock);
    cache.emplace_back (key, made);
    if (cache.size() > kGrainCacheSize)
        cache.erase (cache.begin());
    return made;
}

// J_m(x) = (1/pi) * integral over 0..pi of cos(m t - x sin t): a 64-point midpoint sum.
double besselJ (int m, double x)
{
    constexpr int n = 64;
    double s = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = kPi * (i + 0.5) / n;
        s += std::cos (m * t - x * std::sin (t));
    }
    return s / n;
}

// Membrane modes m = 0..4, n = 0..2: z is the (n+1)-th zero of J_m.
struct Mode
{
    int m, n;
    double z;
};
const std::array<Mode, 15>& modes()
{
    static const std::array<Mode, 15> table { {
        { 0, 0, 2.404825557695773 }, { 0, 1, 5.520078110286311 }, { 0, 2, 8.653727912911013 },
        { 1, 0, 3.831705970207512 }, { 1, 1, 7.015586669815619 }, { 1, 2, 10.17346813506272 },
        { 2, 0, 5.135622301840683 }, { 2, 1, 8.417244140399857 }, { 2, 2, 11.61984117214906 },
        { 3, 0, 6.380161895923984 }, { 3, 1, 9.761023129981670 }, { 3, 2, 13.01520072169843 },
        { 4, 0, 7.588342434503805 }, { 4, 1, 11.06470948850119 }, { 4, 2, 14.37253667161759 },
    } };
    return table;
}

// The mesh's ring radii: 24 from 0.04 to 0.965, and the edge.
const std::array<double, HeadMesh::kRings>& ringRadii()
{
    static const auto table = [] {
        std::array<double, HeadMesh::kRings> t {};
        for (int i = 0; i < 24; ++i)
            t[(size_t) i] = 0.04 + (0.965 - 0.04) * i / 23.0;
        t[24] = 1.0;
        return t;
    }();
    return table;
}

// The modes' shapes on the mesh: J_m(z r) for each ring, divided by its largest magnitude.
struct ModeShapes
{
    std::array<std::array<double, HeadMesh::kRings>, 15> j {};
    ModeShapes()
    {
        const auto& rr = ringRadii();
        for (size_t k = 0; k < 15; ++k)
        {
            double mx = 1e-6;
            for (size_t i = 0; i < rr.size(); ++i)
            {
                j[k][i] = besselJ (modes()[k].m, modes()[k].z * rr[i]);
                mx = std::max (mx, std::abs (j[k][i]));
            }
            for (auto& x : j[k])
                x /= mx;
        }
    }
};
const ModeShapes& modeShapes()
{
    static const ModeShapes s;
    return s;
}

// Convex hull (Andrew's monotone chain), counter-clockwise in screen coordinates.
std::vector<Pt> convexHull (std::vector<Pt> p)
{
    std::sort (p.begin(), p.end(), [] (Pt a, Pt b) { return a.x < b.x || (! (b.x < a.x) && a.y < b.y); });
    if (p.size() < 3)
        return p;
    auto crossZ = [] (Pt o, Pt a, Pt b) { return (double) (a.x - o.x) * (b.y - o.y) - (double) (a.y - o.y) * (b.x - o.x); };
    std::vector<Pt> h (2 * p.size());
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i)
    {
        while (k >= 2 && crossZ (h[k - 2], h[k - 1], p[i]) <= 0)
            --k;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i > 0; --i)
    {
        while (k >= t && crossZ (h[k - 2], h[k - 1], p[i - 1]) <= 0)
            --k;
        h[k++] = p[i - 1];
    }
    h.resize (k - 1);
    return h;
}

// Whether p lies inside a convex polygon from convexHull (on every edge's inner side).
bool insideConvex (const std::vector<Pt>& h, Pt p)
{
    if (h.size() < 3)
        return false;
    for (size_t i = 0; i < h.size(); ++i)
    {
        const Pt a = h[i], b = h[(i + 1) % h.size()];
        if ((double) (b.x - a.x) * (p.y - a.y) - (double) (b.y - a.y) * (p.x - a.x) < 0)
            return false;
    }
    return true;
}

void basis (V3 axis, V3& a, V3& u, V3& v)
{
    a = normalised (axis);
    const V3 t = std::abs (a.z) < 0.9 ? V3 { 0, 0, 1 } : V3 { 1, 0, 0 };
    u = normalised (cross (a, t));
    v = cross (a, u);
}
} // namespace

// =====================================================================================
Camera::Camera (V3 position, V3 look, double focal, double centreX, double centreY)
    : pos (position), F (focal), cx (centreX), cy (centreY)
{
    f = normalised (look - pos);
    r = normalised (cross (f, { 0, 0, 1 }));
    u = cross (r, f);
}

Camera Camera::orbit (V3 target, double elevDeg, double dist, double F, double cx, double cy, double azDeg)
{
    const double e = elevDeg * kPi / 180, z = azDeg * kPi / 180;
    const V3 pos = target + dist * V3 { std::sin (z) * std::cos (e), -std::cos (z) * std::cos (e), std::sin (e) };
    return Camera (pos, target, F, cx, cy);
}

Pt Camera::project (V3 p) const noexcept
{
    const V3 d = p - pos;
    const double z = std::max (1e-6, dot (d, f));
    return { (float) (cx + F * dot (d, r) / z), (float) (cy - F * dot (d, u) / z) };
}

// =====================================================================================
// The mockup's Drum.draw(), with the same names.
struct DrumBuilder
{
    const DrumSpec& s;
    const Camera& cam;
    double bright;
    const Palette& pal;
    bool withGrain;
    V3 C, a, u, v;
    double r, d;
    // the gong drum: its head's radius and the height of its bearing edge above the shell's
    double rh, hh;
    DrumDrawing out;

    DrumBuilder (const DrumSpec& spec, const Camera& camera, double sel, Finish finish, bool grain)
        : s (spec), cam (camera), bright (1.0 + 0.35 * sel), pal (paletteOf (finish)), withGrain (grain)
    {
        C = s.C;
        basis (s.axis, a, u, v);
        r = s.r;
        d = s.d;
        rh = s.headR > 0 ? s.headR : r;
        hh = s.kind == Kind::gong ? 0.6 : 0.0;
    }

    bool vis (V3 P, V3 n) const { return dot (cam.pos - P, n) > 0; }
    V3 ringPoint (double rr, double off, double t) const { return C + off * a + rr * (std::cos (t) * u + std::sin (t) * v); }
    V3 normalAt (double t) const { return std::cos (t) * u + std::sin (t) * v; }

    void line (V3 p, V3 q, Rgb c, float w = 1, bool side = false)
    {
        out.segments.push_back ({ cam.project (p), cam.project (q), c, w, side });
    }
    void line2 (Pt p, Pt q, Rgb c, float w = 1, bool side = false) { out.segments.push_back ({ p, q, c, w, side }); }
    void poly (const std::vector<V3>& pts, Rgb c, float w = 1, bool side = false, bool closed = false)
    {
        if (pts.size() < 2)
            return;
        Pt prev = cam.project (pts[0]);
        const Pt first = prev;
        for (size_t i = 1; i < pts.size(); ++i)
        {
            const Pt p = cam.project (pts[i]);
            line2 (prev, p, c, w, side);
            prev = p;
        }
        if (closed)
            line2 (prev, first, c, w, side);
    }

    Rgb chromeCol (V3 n) const { return kChrome * (0.75 + 0.35 * std::max (0.0, dot (n, kLight))); }

    // The grain's seam: from the top of a top lug diagonally down to the bottom of the next bottom
    // lug (single-headed drums: to the bottom edge under the next lug), on the camera's side,
    // leaning down to the left on screen as on the brochure's shell (Kris, 2026-11-26).
    void seam (double& tTop, double& tBot, double& hTop, double& hBot) const
    {
        const int nl = std::max (3, s.lugs);
        double y0 = 0.45, Lg = std::min (1.7, 0.9 + 0.1 * d);
        if (s.kind == Kind::kick)
        {
            y0 = 1.7;
            Lg = 1.9;
        }
        if (s.kind == Kind::octo)
            Lg = 1.6;
        if (s.kind == Kind::gong)
        {
            y0 = kGongLugY0;
            Lg = 1.6;
        }
        (void) Lg;
        const bool twoEnds = s.kind != Kind::octo && s.kind != Kind::gong;
        // the top lug's top end (its flat end, by the hoop) and the bottom lug's bottom end
        hTop = 0.45 + y0;
        hBot = twoEnds ? d - (0.45 + y0) : d;
        int best = 0, dir = 1;
        double bestF = -2;
        const int strainerGap = s.kind == Kind::snare ? frontGap() : -1;
        for (int k = 0; k < nl; ++k)
            for (int sg : { -1, 1 })
            {
                const double tk = 2 * kPi * (k + 0.5) / nl, tn = tk + sg * 2 * kPi / nl;
                // not across a snare's throw-off
                const int gapCrossed = ((sg > 0 ? k + 1 : k) % nl + nl) % nl;
                if (gapCrossed == strainerGap)
                    continue;
                const Pt sTop = cam.project (C - hTop * a + r * normalAt (tk)), sBot = cam.project (C - hBot * a + r * normalAt (tn));
                if (sBot.x >= sTop.x)
                    continue; // down to the left
                const V3 mid = normalAt (0.5 * (tk + tn));
                const V3 vd = cam.pos - (C - 0.5 * d * a + r * mid);
                const double f = dot (vd, mid) / length (vd);
                if (f > bestF)
                {
                    bestF = f;
                    best = k;
                    dir = sg;
                }
            }
        tTop = 2 * kPi * (best + 0.5) / nl;
        tBot = tTop + dir * 2 * kPi / nl;
    }

    // Which lug gap (angle 2 pi k / nl) faces the camera most: the snares' throw-off goes there.
    double facing (double t) const
    {
        const V3 n = normalAt (t);
        const V3 vd = cam.pos - (C - 0.5 * d * a + r * n);
        return dot (vd, n) / length (vd);
    }
    int frontGap() const
    {
        const int nl = std::max (3, s.lugs);
        int best = 0;
        for (int k = 1; k < nl; ++k)
            if (facing (2 * kPi * k / nl) > facing (2 * kPi * best / nl))
                best = k;
        return best;
    }
    // the gap next to gap g that is to the right on screen
    int gapRight (int g) const
    {
        const int nl = std::max (3, s.lugs);
        const double tg = 2 * kPi * g / nl;
        const Pt p0 = cam.project (C - 0.5 * d * a + r * normalAt (tg));
        const Pt pp = cam.project (C - 0.5 * d * a + r * normalAt (tg + 2 * kPi / nl));
        return pp.x > p0.x ? (g + 1) % nl : (g + nl - 1) % nl;
    }

    void shell()
    {
        // the hoops cover the shell's ends (not the gong drum's: its hoop stands off the shell)
        const double cov = s.kind != Kind::kick && s.kind != Kind::gong ? 0.45 : 0.0;
        const bool two = s.kind != Kind::octo && s.kind != Kind::gong;
        const double x0 = cov / d, x1 = 1 - (two ? cov / d : 0.0);
        if (withGrain)
        {
            GrainKey key { (int) s.kind, r, d, x0, x1, 0, 0, 0, 0 };
            seam (key.tTop, key.tBot, key.hTop, key.hBot);
            const auto grain = grainFor (key);
            // the shell's centre seen from the camera, for the facing test: a point at angle t
            // faces the camera if (cam - centre) . n > r
            const V3 toCam = cam.pos - (C - 0.5 * d * a);
            const double cu = dot (toCam, u), cv = dot (toCam, v);
            const double lu = dot (kLight, u), lv = dot (kLight, v), ru = dot (cam.r, u), rv = dot (cam.r, v);
            for (const auto& g : *grain)
            {
                if (cu * g.c0 + cv * g.s0 <= r || cu * g.c1 + cv * g.s1 <= r)
                    continue;
                const double mc = 0.5 * (g.c0 + g.c1), ms = 0.5 * (g.s0 + g.s1), x = 0.5 * (g.h0 + g.h1) / d;
                const double l = std::clamp (mc * lu + ms * lv, 0.0, 1.0), sd = std::abs (mc * ru + ms * rv);
                const double burst = std::pow (std::max (0.0, 1 - std::abs (x - 0.45) / 0.75), 0.8);
                Rgb c = pal.dark + (pal.body - pal.dark) * burst;
                c = c + (pal.hi - c) * (std::max (0.0, l - 0.55) * 1.4 * burst);
                const double sd2 = sd * sd;
                c = c * ((0.7 + 0.65 * l) * (1 - 0.35 * sd2 * sd2) * bright * g.k);
                line2 (cam.project (C - (double) g.h0 * a + r * ((double) g.c0 * u + (double) g.s0 * v)),
                       cam.project (C - (double) g.h1 * a + r * ((double) g.c1 * u + (double) g.s1 * v)), c);
            }
        }
        // the shell's two end edges (the batter edge all round when the head faces the camera)
        for (int e = 0; e < 2; ++e)
        {
            const double off = e == 0 ? 0.0 : -d, kcol = e == 0 ? 0.8 : 0.55;
            const Rgb c = (pal.dark * (1.4 * kcol) + pal.body * 0.35) * bright;
            for (int i = 0; i < 96; ++i)
            {
                const double t0 = 2 * kPi * i / 96, t1 = 2 * kPi * (i + 1) / 96;
                const V3 P = ringPoint (r, off, t0);
                if (vis (P, normalAt (t0)) || (e == 0 && vis (C, a)))
                    line (P, ringPoint (r, off, t1), c);
            }
        }
    }

    // A Starclassic lug: flat end toward the hoop, round end away, a quarter round in profile,
    // on a backing plate. Returns false if it is well behind the drum; `end`: where the rod goes in.
    bool bulletLug (double t, double off, double sgn, double y0, double Lg, V3& end, double w = 0.36, double D = 1.05)
    {
        const V3 n = normalAt (t), tg = -std::sin (t) * u + std::cos (t) * v, ax = sgn * a;
        const V3 top = C + off * a + r * n - sgn * (0.45 + y0) * a;
        const V3 vd = cam.pos - (top - ax * (0.5 * Lg));
        const double f = dot (vd, n) / length (vd);
        if (f < -0.35)
            return false;
        const bool sideLayer = f <= 0.1;
        auto wy = [&] (double y) { return y > Lg - w ? std::sqrt (std::max (w * w - (y - (Lg - w)) * (y - (Lg - w)), 0.0)) + 1e-6 : w; };
        auto zy = [&] (double y) { return D * std::sqrt (std::max (0.0, 1 - std::pow (y / Lg, 2.2))); };
        auto P = [&] (double x, double y) {
            const double ww = wy (y);
            const double z = zy (y) * std::pow (std::max (0.0, 1 - std::pow (std::abs (x / ww), 3)), 0.35);
            return top - y * ax + x * tg + (z + 0.06) * n;
        };
        const Rgb col = chromeCol (n);
        std::vector<double> ys (21);
        for (int i = 0; i < 21; ++i)
            ys[(size_t) i] = Lg * i / 20;
        std::vector<V3> pts;
        // the flat end face and cross-sections down the lug
        const double cuts[3] = { 0.0, 0.3 * Lg, 0.6 * Lg };
        for (int k = 0; k < 3; ++k)
        {
            pts.clear();
            const double y = cuts[k], ww = wy (y);
            for (int i = 0; i < 15; ++i)
                pts.push_back (P (-ww + 2 * ww * i / 14, y));
            poly (pts, col * ((k == 0 ? 1.0 : 0.7) * bright), k == 0 ? 2.0f : 1.0f, sideLayer);
        }
        // lengthwise lines
        for (int fi = 0; fi < 5; ++fi)
        {
            const double fx = std::array<double, 5> { -0.98, -0.5, 0.0, 0.5, 0.98 }[(size_t) fi];
            pts.clear();
            for (double y : ys)
                if (wy (y) >= std::abs (fx) * w * 0.999)
                    pts.push_back (P (fx * w, y));
            Rgb c = col * (fi == 2 ? 1.2 : 0.8);
            c = { std::min (c.r, 255.0f), std::min (c.g, 255.0f), std::min (c.b, 255.0f) };
            poly (pts, c * bright, 1, sideLayer);
        }
        // the plan outline on the backing plate
        pts.clear();
        for (double y : ys)
            pts.push_back (top - y * ax + (-wy (y)) * tg + 0.06 * n);
        for (auto it = ys.rbegin(); it != ys.rend(); ++it)
            pts.push_back (top - *it * ax + wy (*it) * tg + 0.06 * n);
        poly (pts, col * (0.7 * bright), 1, sideLayer, true);
        // the backing plate, a larger U
        const double m = 0.13;
        pts.clear();
        auto plate = [&] (double x, double y) { return top - y * ax + x * tg + 0.03 * n; };
        pts.push_back (plate (-(w + m), -m));
        for (double y : ys)
            pts.push_back (plate (-(wy (y) + m), y));
        pts.push_back (plate (0.0, Lg + m));
        for (auto it = ys.rbegin(); it != ys.rend(); ++it)
            pts.push_back (plate (wy (*it) + m, *it));
        pts.push_back (plate (w + m, -m));
        poly (pts, col * (0.45 * bright), 1, sideLayer, true);
        // the swivel nut in the flat end
        pts.clear();
        for (auto [q0, q1] : { std::pair<double, double> { -0.14, 0.45 }, { 0.14, 0.45 }, { 0.14, 0.8 }, { -0.14, 0.8 } })
            pts.push_back (top + q0 * tg + q1 * n);
        poly (pts, col * (0.9 * bright), 1, sideLayer, true);
        end = top + 0.62 * n;
        return true;
    }

    void hardware()
    {
        const int nlug = s.lugs;
        std::vector<double> lugT;
        for (int k = 0; k < nlug; ++k)
            lugT.push_back (2 * kPi * (k + 0.5) / nlug);
        std::vector<std::pair<double, double>> ends { { 0.0, 1.0 } };
        if (s.kind != Kind::octo && s.kind != Kind::gong)
            ends.push_back ({ -d, -1.0 });

        auto frame = [&] (double t, double sgn, V3& n, V3& tg, V3& ax) {
            n = normalAt (t);
            tg = -std::sin (t) * u + std::cos (t) * v;
            ax = sgn * a;
        };

        if (s.kind == Kind::gong)
        {
            gongHardware (lugT);
            return;
        }

        if (s.kind == Kind::kick)
        {
            const Rgb wood = pal.dark * 0.9 + pal.body * 0.75;
            for (auto [off, sgn] : ends)
            {
                const bool face = vis (C + off * a, sgn * a);
                struct R { double dr, h; Rgb col; float th; };
                for (const R& q : { R { 0.0, 0.15, wood * 0.9, 1 }, R { 0.6, 0.15, wood, 1 }, R { 0.6, 1.6, wood, 2 },
                                    R { 0.0, 1.6, wood * 0.9, 1 }, R { 0.6, 0.35, kChrome * 0.75, 1 } })
                    ring (q.dr, q.h, off, sgn, q.col, q.th, face && q.h > 1.0);
                for (double t : lugT)
                {
                    V3 n, tg, ax;
                    frame (t, sgn, n, tg, ax);
                    const V3 cpos = C + off * a + (r + 0.6) * n;
                    const V3 vd = cam.pos - cpos;
                    const double f = dot (vd, n) / length (vd);
                    if (f < -0.35 && ! face)
                        continue;
                    const bool sideLayer = ! (f > 0.1 || face);
                    // the claw over the hoop and its lip over the edge
                    std::vector<V3> claw;
                    for (auto q : { V3 { 0.15, -0.4, -0.3 }, V3 { 0.15, 1.3, -0.3 }, V3 { -0.15, 1.75, -0.3 }, V3 { -0.15, 1.75, 0.3 },
                                    V3 { 0.15, 1.3, 0.3 }, V3 { 0.15, -0.4, 0.3 } })
                        claw.push_back (cpos + q.x * n + q.y * ax + q.z * tg);
                    poly (claw, chromeCol (n) * bright, 1, sideLayer, true);
                    if (f < -0.35)
                        continue;
                    V3 end;
                    if (bulletLug (t, off, sgn, 1.7, 1.9, end, 0.4, 1.1))
                        line (cpos + 0.15 * n - 0.4 * ax, end + 0.25 * n, kChromeD * (0.9 * bright), 1, sideLayer);
                }
            }
            return;
        }

        // die-cast hoops, two tiers: the inner tier near the head stands higher; the outer
        // tier sticks out farther and swells into an ear at each rod
        constexpr int NA = 241;
        std::array<double, NA> tt {}, ew {};
        for (int i = 0; i < NA; ++i)
        {
            tt[(size_t) i] = 2 * kPi * i / (NA - 1);
            double k = 0;
            for (double tl : lugT)
            {
                const double dd = std::remainder (tt[(size_t) i] - tl, 2 * kPi) * r;
                const double ewid = s.kind == Kind::octo ? 0.6 : 0.95;  // octobans: small ears
                k = std::max (k, std::pow (std::clamp (1 - (dd / ewid) * (dd / ewid), 0.0, 1.0), 1.5));
            }
            ew[(size_t) i] = k;
        }
        // the far end's hoop sticks out past the shell: beyond the shell's outline on screen it
        // shows until the shell covers it, not only where it faces the camera (the floor toms'
        // bottom hoops stopped short on the left, Kris 2026-11-27)
        const std::vector<Pt> shellOutline = hull (0.0, 0.0, -d);
        bool farEnd = false;
        auto hoopLine = [&] (double dr0, double drEw, double h0, double hEw, double off, double sgn, Rgb col, float th, bool allRound) {
            Pt prev {};
            V3 prevP {};
            for (int i = 0; i < NA; ++i)
            {
                const double t = tt[(size_t) i];
                const V3 n = normalAt (t);
                const V3 P = C + (off + sgn * (h0 + hEw * ew[(size_t) i])) * a + (r + dr0 + drEw * ew[(size_t) i]) * n;
                const Pt sp = cam.project (P);
                if (i > 0
                    && (allRound || vis (prevP, normalAt (tt[(size_t) i - 1]))
                        || (farEnd && ! insideConvex (shellOutline, { 0.5f * (prev.x + sp.x), 0.5f * (prev.y + sp.y) }))))
                    line2 (prev, sp, col * bright, th);
                prev = sp;
                prevP = P;
            }
        };
        for (auto [off, sgn] : ends)
        {
            const bool face = vis (C + off * a, sgn * a);
            farEnd = ! face;
            if (s.kind == Kind::octo)
            {
                // a 6" hoop: the same shape in fewer, finer lines (the full set crowds together)
                hoopLine (0.10, 0, 0.50, 0, off, sgn, kChrome * 1.0, 1, face);           // rounded top
                hoopLine (0.30, 0, 0.14, 0, off, sgn, kChrome * 0.6, 1, face);           // the step down
                hoopLine (0.40, 0.16, 0.10, 0, off, sgn, kChrome * 0.95, 1, face);       // outer top edge, ears
                hoopLine (0.44, 0.18, -0.30, -0.16, off, sgn, kChrome * 0.8, 1, false);  // lower edge into the ears
            }
            else
            {
                hoopLine (0.03, 0, 0.52, 0, off, sgn, kChrome * 0.85, 1, face);          // inner tier: inner edge
                hoopLine (0.13, 0, 0.58, 0, off, sgn, kChrome * 1.0, 2, face);           // inner tier: rounded top
                hoopLine (0.24, 0, 0.50, 0, off, sgn, kChrome * 0.85, 1, face);          // inner tier: outer edge
                hoopLine (0.27, 0, 0.20, 0, off, sgn, kChrome * 0.55, 1, face);          // the step down
                hoopLine (0.32, 0, 0.16, 0, off, sgn, kChrome * 0.8, 1, face);           // outer tier: top, inner
                hoopLine (0.58, 0.30, 0.14, 0, off, sgn, kChrome * 1.0, 2, face);        // outer tier: top edge
                hoopLine (0.64, 0.30, -0.10, 0, off, sgn, kChrome * 0.55, 1, false);     // outer tier: face
                hoopLine (0.64, 0.34, -0.42, -0.30, off, sgn, kChrome * 0.9, 1, false);  // lower edge into the ears
            }
            for (double t : lugT)
            {
                V3 n, tg, ax;
                frame (t, sgn, n, tg, ax);
                const V3 e0 = C + off * a + (r + (s.kind == Kind::octo ? 0.45 : 0.62)) * n;
                const V3 vd = cam.pos - e0;
                const double f = dot (vd, n) / length (vd);
                if (f < -0.35)
                    continue;
                const bool sideLayer = f <= 0.1;
                const Rgb cc = chromeCol (n);
                // the washer and the rod head on the ear
                const V3 rc = e0 + 0.18 * n + 0.14 * ax;
                struct W { double hh, rad, k; };
                for (const W& q : { W { 0.0, 0.26, 0.7 }, W { 0.06, 0.17, 1.0 }, W { 0.34, 0.17, 1.0 } })
                {
                    const int np = q.hh > 0 ? 7 : 17;
                    std::vector<V3> pts;
                    for (int i = 0; i < np; ++i)
                    {
                        const double x = 2 * kPi * i / (np - 1) + 0.4;
                        pts.push_back (rc + q.hh * ax + q.rad * (std::cos (x) * n + std::sin (x) * tg));
                    }
                    poly (pts, cc * (q.k * bright), 1, sideLayer);
                }
                for (double x : { 0.4, 0.4 + 2 * kPi / 3, 0.4 + 4 * kPi / 3 })
                {
                    const V3 pp = rc + 0.17 * (std::cos (x) * n + std::sin (x) * tg);
                    line (pp + 0.06 * ax, pp + 0.34 * ax, cc * (0.9 * bright), 1, sideLayer);
                }
                // the lug, and the rod from under the ear into its flat end (octobans: the tom
                // lug; shorter, it looked tilted, Kris 2026-11-24)
                const double y0 = 0.45;
                const double Lg = s.kind == Kind::octo ? 1.6 : std::min (1.7, 0.9 + 0.1 * d);
                V3 end;
                if (bulletLug (t, off, sgn, y0, Lg, end))
                    line (rc - 0.7 * ax, end, kChromeD * (0.95 * bright), 1, sideLayer);
            }
        }
        if (s.kind == Kind::octo)
            ring (0.0, 0.0, -d, 1, pal.dark * 1.5 + pal.body * 0.4, 1, vis (C - d * a, -1.0 * a));
    }

    // The gong drum (TAMA's, Kris's photo, 2026-11-24): a 22" head mounted timpani style on a
    // 20" shell with no bottom head. A support ring flares from the shell's top edge out to the
    // 22" bearing edge; the head's collar is pulled down over it by a thin flat counter hoop whose
    // top sits below the playing surface. The lugs sit on the shell just under the hoop; their
    // rods angle out through ears on the hoop, their tips above it (2026-11-25).
    static constexpr double kGongLugY0 = 1.2;
    void gongHardware (const std::vector<double>& lugT)
    {
        const bool headFaces = vis (C, a);
        // the support ring and its flare, in short spokes
        ring (0.05, 0.10, 0.0, 1.0, kChrome * 0.45, 1, headFaces);
        for (int k = 0; k < 36; ++k)
        {
            const double t = 2 * kPi * k / 36;
            if (! (headFaces || vis (ringPoint (r, 0.0, t), normalAt (t))))
                continue;
            line (ringPoint (r + 0.05, 0.10, t), ringPoint (rh - 0.05, hh - 0.08, t), kChrome * (0.35 * bright));
        }
        // the head's collar, then the counter hoop: top edge, band face, bottom edge
        ring (rh - r + 0.10, hh - 0.22, 0.0, 1.0, kHead * 0.48, 1, false);
        ring (rh - r + 0.22, hh - 0.30, 0.0, 1.0, kChrome * 1.0, 1, headFaces);
        ring (rh - r + 0.24, hh - 0.55, 0.0, 1.0, kChrome * 0.5, 1, false);
        ring (rh - r + 0.24, hh - 0.80, 0.0, 1.0, kChrome * 0.85, 1, false);
        for (double t : lugT)
        {
            const V3 n = normalAt (t), tg = -std::sin (t) * u + std::cos (t) * v;
            const Rgb cc = chromeCol (n);
            // As TAMA's (Kris's photo, 2026-11-25): each rod runs from its lug, just under the
            // hoop, up and out through an ear on the hoop, its tip standing above the hoop. The ear
            // and the tip show wherever the hoop does (all round when the head faces the camera),
            // so every tension point shows; the lug only on the near side.
            const V3 earLo = ringPoint (rh + 0.40, hh - 0.80, t), earHi = ringPoint (rh + 0.40, hh - 0.30, t);
            const V3 tip = ringPoint (rh + 0.40, hh + 0.25, t);
            const bool hoopSeen = headFaces || vis (earLo, n);
            V3 end;
            const bool lugDrawn = bulletLug (t, 0.0, 1.0, kGongLugY0, 1.6, end, 0.36, 0.95); // its flat end 1.65" below the shell's edge
            if (hoopSeen || lugDrawn)
            {
                const bool sideLayer = ! hoopSeen;
                // the ear: a small bracket off the hoop's band
                for (double o : { -0.17, 0.17 })
                    poly ({ ringPoint (rh + 0.24, hh - 0.80, t) + o * tg, earLo + o * tg, earHi + o * tg, ringPoint (rh + 0.24, hh - 0.30, t) + o * tg },
                          cc * bright, 1, sideLayer);
                line (earHi - 0.17 * tg, earHi + 0.17 * tg, cc * (0.9 * bright), 1, sideLayer);
                // the rod's tip above the hoop, and its T-handle
                line (earLo, tip, kChrome * (0.85 * bright), 1, sideLayer);
                line (tip - 0.2 * tg, tip + 0.2 * tg, cc * (0.9 * bright), 1, sideLayer);
            }
            if (lugDrawn)
            {
                // the rod from the lug's flat end, angled out to the ear
                const V3 vd = cam.pos - ringPoint (r, -0.45 - kGongLugY0 - 0.7, t);
                line (end, earLo, kChrome * (0.8 * bright), 1, dot (vd, n) / length (vd) <= 0.1);
            }
        }
    }

    // A ring r + dr around the axis, h out from the end `off` (sgn: out of the drum).
    void ring (double dr, double h, double off, double sgn, Rgb col, float th, bool allRound)
    {
        constexpr int n = 121;
        for (int i = 0; i < n - 1; ++i)
        {
            const double t0 = 2 * kPi * i / (n - 1), t1 = 2 * kPi * (i + 1) / (n - 1);
            const V3 P = ringPoint (r + dr, off + sgn * h, t0);
            if (allRound || vis (P, normalAt (t0)))
                line (P, ringPoint (r + dr, off + sgn * h, t1), col * bright, th);
        }
    }

    // The snares' throw-off (Kris's photo of his Starclassic snare, mockup approved 2026-11-26) in
    // the lug gap facing the camera, the butt end opposite: the lever up with the snares on, down
    // with them off (the SNARES switch). Inches: x around the shell (+ to tg), H down from the
    // batter edge, o out from the shell.
    void snareParts()
    {
        const int nl = std::max (3, s.lugs);
        const double ts = 2 * kPi * frontGap() / nl;
        throwOff (ts, s.snaresOff);
        butt (ts + kPi);
    }
    struct Local
    {
        V3 P0, n, tg, ax;
        V3 at (double x, double H, double o) const { return P0 + x * tg + H * ax + o * n; }
    };
    Local localAt (double t) const
    {
        Local L;
        L.n = normalAt (t);
        L.tg = -std::sin (t) * u + std::cos (t) * v;
        L.ax = -1.0 * a;
        L.P0 = C + r * L.n;
        return L;
    }
    // a box from (x0,H0,o0) to (x1,H1,o1): its front face, its edges back to the shell and its top
    void box (const Local& L, double x0, double x1, double H0, double H1, double o0, double o1, Rgb c, bool round = false)
    {
        std::vector<V3> f;
        if (round)
        {
            const double rr = std::min (0.18, 0.3 * std::min (x1 - x0, H1 - H0));
            const double cx[4] = { x1 - rr, x1 - rr, x0 + rr, x0 + rr }, cy[4] = { H0 + rr, H1 - rr, H1 - rr, H0 + rr };
            const double a0[4] = { -0.5 * kPi, 0, 0.5 * kPi, kPi };
            for (int q = 0; q < 4; ++q)
                for (int i = 0; i <= 4; ++i)
                {
                    const double t = a0[q] + 0.5 * kPi * i / 4;
                    f.push_back (L.at (cx[q] + rr * std::cos (t), cy[q] + rr * std::sin (t), o1));
                }
        }
        else
            f = { L.at (x0, H0, o1), L.at (x1, H0, o1), L.at (x1, H1, o1), L.at (x0, H1, o1) };
        poly (f, c, 1, false, true);
        for (double x : { x0, x1 })
            for (double H : { H0, H1 })
                line (L.at (x, H, o1), L.at (x, H, o0), c * 0.8);
        line (L.at (x0, H0, o0), L.at (x1, H0, o0), c * 0.7);
    }
    void circle (V3 c0, V3 e1, V3 e2, double rr, Rgb col, int n = 20)
    {
        std::vector<V3> pts;
        for (int i = 0; i < n; ++i)
        {
            const double t = 2 * kPi * i / n;
            pts.push_back (c0 + rr * std::cos (t) * e1 + rr * std::sin (t) * e2);
        }
        poly (pts, col, 1, false, true);
    }
    void throwOff (double ts, bool off)
    {
        const Local L = localAt (ts);
        if (! vis (L.at (0, 0.5 * d, 0), L.n))
            return;
        const Rgb ch = chromeCol (L.n) * bright, chD = kChromeD * bright;
        const double bTop = d - 3.5, bBot = d - 1.3; // the body
        // the back plate and the body, the blank plate on its front
        box (L, -0.72, 0.72, bTop - 0.1, bBot + 0.1, 0.0, 0.08, chD * 0.8, true);
        box (L, -0.62, 0.62, bTop, bBot, 0.08, 0.85, ch, true);
        box (L, -0.45, 0.45, bTop + 1.0, bBot - 0.25, 0.85, 0.9, ch * 0.85, true);
        // the knurled tension knob on top, its axis along the shell
        {
            const double kx = 0.18, ko = 0.5, kr = 0.27, h0 = bTop, h1 = bTop - 0.62;
            circle (L.at (kx, h0, ko), L.tg, L.n, kr, ch);
            circle (L.at (kx, h1, ko), L.tg, L.n, kr, ch);
            circle (L.at (kx, h1 - 0.04, ko), L.tg, L.n, kr * 0.6, ch * 0.9);
            for (int i = 0; i < 10; ++i)
            {
                const double t = 2 * kPi * i / 10;
                const V3 e = kr * (std::cos (t) * L.tg + std::sin (t) * L.n);
                line (L.at (kx, h0, ko) + e, L.at (kx, h1, ko) + e, ch * 0.8);
            }
            // its threaded post down into the body
            line (L.at (kx, h0, ko), L.at (kx, bTop + 0.05, ko), chD);
        }
        // the lever: pivot at the body's upper left, up and to the left when on, down when off
        {
            const double px = -0.5, pH = bTop + 0.55, po = 0.95;
            double dx = -0.72, dH = off ? 0.69 : -0.69;
            const double nrm = std::sqrt (dx * dx + dH * dH);
            dx /= nrm;
            dH /= nrm;
            const double qx = -dH, qH = dx; // across the lever
            auto P = [&] (double along, double across, double o) { return L.at (px + along * dx + across * qx, pH + along * dH + across * qH, o); };
            circle (L.at (px, pH, po), L.tg, -1.0 * L.ax, 0.2, ch);
            poly ({ P (0, -0.1, po), P (1.5, -0.1, po + 0.15), P (1.5, 0.1, po + 0.15), P (0, 0.1, po) }, ch, 1, false, true);
            // the grip: a block with grooves across it
            const double g0 = 1.4, g1 = 2.9, gw = 0.32, o0 = po, o1 = po + 0.45;
            std::vector<V3> top { P (g0, -gw, o1), P (g1, -gw, o1), P (g1, gw, o1), P (g0, gw, o1) };
            poly (top, kRubber * bright, 1, false, true);
            for (double al : { g0, g1 })
                for (double ac : { -gw, gw })
                    line (P (al, ac, o1), P (al, ac, o0), kRubber * (0.8 * bright));
            poly ({ P (g0, -gw, o0), P (g1, -gw, o0), P (g1, gw, o0), P (g0, gw, o0) }, kRubber * (0.7 * bright), 1, false, true);
            for (int i = 1; i <= 4; ++i)
            {
                const double al = g0 + 0.35 + (g1 - g0 - 0.5) * i / 5;
                line (P (al, -gw * 0.8, o1 + 0.01), P (al, gw * 0.8, o1 + 0.01), kRubber * (1.25 * bright));
            }
        }
        // the slide down to the clamp under the bottom hoop, its two screws, the snare cord
        box (L, -0.42, 0.42, bBot, d + 0.35, 0.1, 0.55, chD);
        box (L, -0.8, 0.8, d + 0.35, d + 0.95, 0.05, 0.75, ch, true);
        for (double x : { -0.55, 0.55 })
            circle (L.at (x, d + 0.72, 0.77), L.tg, -1.0 * L.ax, 0.12, ch * 0.9, 12);
        for (double x : { -0.3, 0.3 })
        {
            std::vector<V3> cord;
            for (int i = 0; i <= 12; ++i)
            {
                const double f = i / 12.0;
                const double o = 0.35 - 2.4 * f, H = d + 0.95 + (off ? 0.55 : 0.12) * std::sin (kPi * f) - 0.7 * f;
                cord.push_back (L.at (x * (1 - 0.6 * f) + 0.15 * std::sin (6 * f) * (x > 0 ? 1 : -1), H, o));
            }
            poly (cord, kChromeD * (0.75 * bright));
        }
    }
    void butt (double tb)
    {
        const Local L = localAt (tb);
        if (! vis (L.at (0, 0.5 * d, 0), L.n))
            return;
        const Rgb ch = chromeCol (L.n) * bright;
        box (L, -0.55, 0.55, d - 1.9, d - 0.5, 0.05, 0.6, ch, true);
        box (L, -0.8, 0.8, d + 0.35, d + 0.95, 0.05, 0.75, ch, true);
    }

    // The air vent (mockup approved 2026-11-26): a brass grommet in a lug gap facing the camera
    // (snares: the gap to the right of the throw-off; the others: not the seam's gap). The gong
    // drum and the octobans have none (Kris).
    void vent()
    {
        if (s.kind == Kind::octo || s.kind == Kind::gong)
            return;
        const int nl = std::max (3, s.lugs);
        int g = frontGap();
        if (s.kind == Kind::snare)
            g = gapRight (g);
        else
        {
            double tTop, tBot, hTop, hBot;
            seam (tTop, tBot, hTop, hBot);
            const int seamGap = (int) std::lround (0.5 * (tTop + tBot) / (2 * kPi / nl));
            if (((seamGap % nl) + nl) % nl == g)
                g = gapRight (g);
        }
        const double t = 2 * kPi * g / nl;
        const double H = s.kind == Kind::snare ? 0.42 * d : s.kind == Kind::kick ? 0.4 * d : 0.45 * d;
        const Local L = localAt (t);
        if (! vis (L.at (0, H, 0), L.n))
            return;
        const double l = std::clamp (dot (L.n, kLight), 0.0, 1.0);
        const Rgb c = kBrass * ((0.75 + 0.45 * l) * bright);
        circle (L.at (0, H, 0.04), L.tg, L.ax, 0.3, c, 24);
        circle (L.at (0, H, 0.06), L.tg, L.ax, 0.22, c * 1.1, 20);
        circle (L.at (0, H, 0.02), L.tg, L.ax, 0.13, c * 0.6, 16);
    }

    std::vector<Pt> hull (double dr, double front, double back) const { return hull2 (r + dr, front, r + dr, back); }
    std::vector<Pt> hull2 (double rFront, double front, double rBack, double back) const
    {
        std::vector<Pt> pts;
        for (int i = 0; i < 64; ++i)
        {
            const double t = 2 * kPi * i / 63;
            pts.push_back (cam.project (ringPoint (rFront, front, t)));
            pts.push_back (cam.project (ringPoint (rBack, back, t)));
        }
        return convexHull (std::move (pts));
    }

    void headMesh()
    {
        out.headVisible = vis (C, a);
        HeadMesh& h = out.head;
        h.cam = cam;
        const V3 Ch = C + hh * a; // the gong drum's head: on its bearing edge, 22"
        h.C = Ch;
        h.a = a;
        h.u = u;
        h.v = v;
        h.r = rh;
        h.clear = s.kind == Kind::kick;
        h.bright = bright;
        h.ringStep = h.pointStep = s.kind == Kind::octo ? 2 : 1;
        const auto& rr = ringRadii();
        h.rest.resize ((size_t) (HeadMesh::kRings * HeadMesh::kPoints));
        h.restScreen.resize (h.rest.size());
        h.x0 = h.y0 = 1e9f;
        h.x1 = h.y1 = -1e9f;
        for (int i = 0; i < HeadMesh::kRings; ++i)
            for (int j = 0; j < HeadMesh::kPoints; ++j)
            {
                const double t = 2 * kPi * j / HeadMesh::kPoints;
                const V3 P = Ch + rh * rr[(size_t) i] * normalAt (t);
                const size_t k = (size_t) (i * HeadMesh::kPoints + j);
                h.rest[k] = P;
                h.restScreen[k] = cam.project (P);
                h.x0 = std::min (h.x0, h.restScreen[k].x);
                h.x1 = std::max (h.x1, h.restScreen[k].x);
                h.y0 = std::min (h.y0, h.restScreen[k].y);
                h.y1 = std::max (h.y1, h.restScreen[k].y);
            }
        // how far the flex can move a point on the screen, per unit of displacement
        const double k = 0.35 * rh / 7;
        float reach = 0;
        for (int j = 0; j < HeadMesh::kPoints; j += 8)
        {
            const V3 P = h.rest[(size_t) (12 * HeadMesh::kPoints + j)];
            const Pt p0 = cam.project (P), p1 = cam.project (P + k * a);
            reach = std::max (reach, std::hypot (p1.x - p0.x, p1.y - p0.y));
        }
        h.flexReach = reach;
        // the head's point nearest the camera: where a drummer's stick lands
        const V3 toCam = cam.pos - Ch;
        h.near = std::atan2 (dot (toCam, v), dot (toCam, u));
    }

    void bounds()
    {
        out.x0 = out.y0 = 1e9f;
        out.x1 = out.y1 = -1e9f;
        auto add = [this] (Pt p) {
            out.x0 = std::min (out.x0, p.x);
            out.x1 = std::max (out.x1, p.x);
            out.y0 = std::min (out.y0, p.y);
            out.y1 = std::max (out.y1, p.y);
        };
        for (const auto& sgm : out.segments)
        {
            add (sgm.a);
            add (sgm.b);
        }
        if (out.headVisible)
        {
            add ({ out.head.x0, out.head.y0 });
            add ({ out.head.x1, out.head.y1 });
        }
    }

    DrumDrawing build()
    {
        out.segments.reserve (12000);
        shell();
        hardware();
        vent();
        // the gong drum: only its shell hides the hardware behind it (its wider head would hide
        // the lugs at the sides, Kris 2026-11-24)
        out.bodyHull = s.kind == Kind::gong ? hull (0.0, 0.0, -d) : hull (0.25, 0.55, -d - 0.55);
        headMesh();
        if (s.kind == Kind::snare)
            snareParts();
        out.silhouette = s.kind == Kind::gong ? hull2 (rh + 0.5, hh + 0.2, r + 0.9, -d - 0.6) : hull (0.9, 0.6, -d - 0.6);
        out.depth = cam.depth (C - 0.5 * d * a);
        bounds();
        return std::move (out);
    }
};

DrumDrawing drawDrum (const DrumSpec& spec, const Camera& cam, double sel, Finish finish)
{
    return DrumBuilder (spec, cam, sel, finish, true).build();
}

namespace
{
// Without the grain: for measuring (the grain lies inside the shell's edges and hoops).
DrumDrawing drawOutline (const DrumSpec& spec, const Camera& cam)
{
    return DrumBuilder (spec, cam, 0.0, Finish::green, false).build();
}
} // namespace

// =====================================================================================
// The bass drum pedal. Its frame: O on the floor just in front of the batter hoop, F toward the
// drum (the kick's axis, reversed), X to the drummer's right, Z up; inches.
double pedalSwing (double t)
{
    if (t < 0 || t >= 0.2)
        return 0;
    const double k = 1 - t / 0.2;
    return k * k;
}

void addKickPedal (DrumDrawing& out, const DrumSpec& kick, const Camera& cam, double sel, double swing, PedalParts parts)
{
    V3 a, u, v;
    basis (kick.axis, a, u, v);
    const V3 Z { 0, 0, 1 };
    const V3 back = normalised (V3 { a.x, a.y, 0 });
    const V3 F = back * -1.0;
    const V3 X = cross (F, Z);
    const V3 O = V3 { kick.C.x, kick.C.y, kick.C.z - kick.r } + 0.35 * back;
    const double bright = 1.0 + 0.35 * sel;
    const bool fixedParts = parts != PedalParts::moving, movingParts = parts != PedalParts::fixed;
    auto P = [&] (double x, double f, double z) { return O + x * X + f * F + z * Z; };
    auto seg = [&] (V3 p, V3 q, Rgb c, float w = 1) { out.segments.push_back ({ cam.project (p), cam.project (q), c * bright, w, false }); };
    auto path = [&] (const std::vector<V3>& pts, Rgb c, float w = 1, bool closed = false) {
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            seg (pts[i], pts[i + 1], c, w);
        if (closed && pts.size() > 2)
            seg (pts.back(), pts.front(), c, w);
    };
    // a circle of radius rr around c, in the plane of e1 and e2 (an arc from t0 to t1)
    auto arc = [&] (V3 c, V3 e1, V3 e2, double rr, Rgb col, double t0, double t1, int n = 24, float w = 1) {
        std::vector<V3> pts;
        for (int i = 0; i <= n; ++i)
        {
            const double t = t0 + (t1 - t0) * i / n;
            pts.push_back (c + rr * std::cos (t) * e1 + rr * std::sin (t) * e2);
        }
        path (pts, col, w);
    };
    auto circle = [&] (V3 c, V3 e1, V3 e2, double rr, Rgb col, int n = 24, float w = 1) { arc (c, e1, e2, rr, col, 0, 2 * kPi, n, w); };
    // a cylinder along `ax` from c0 (length len, radius rr): its two ends and four sides
    auto cylinder = [&] (V3 c0, V3 ax, V3 e1, V3 e2, double len, double rr, Rgb col, int n = 20) {
        circle (c0, e1, e2, rr, col, n);
        circle (c0 + len * ax, e1, e2, rr, col, n);
        for (int k = 0; k < 4; ++k)
        {
            const double t = 2 * kPi * (k + 0.5) / 4;
            const V3 e = rr * (std::cos (t) * e1 + std::sin (t) * e2);
            seg (c0 + e, c0 + len * ax + e, col * 0.8);
        }
    };
    // a flat plate: its outline (x, f pairs) at height z, `th` thick
    auto slab = [&] (const std::vector<std::pair<double, double>>& pts, double z, double th, Rgb top, Rgb side) {
        std::vector<V3> t, b;
        for (const auto& [x, f] : pts)
        {
            t.push_back (P (x, f, z));
            b.push_back (P (x, f, z - th));
        }
        path (t, top, 1, true);
        path (b, side, 1, true);
        for (size_t i = 0; i < t.size(); ++i)
            seg (t[i], b[i], side);
    };
    const Rgb alu = kChrome, dim = kChromeD * 0.8, dark = kChromeD * 0.45;
    // the single post on the drummer's left; the axle's housing on top; the hub and cam on the axle
    const double postX = -1.95, axleF = -1.9, axleZ = 7.3, camX = 0.95, camR = 1.25;
    // the footboard 13" long (Trick's long board), 2.75" wide (measured from Kris's photos)
    const double heelF = -15.55, toeF = -2.55, heelZ = 0.85, toeZ = 2.7 - 1.4 * swing, bw = 1.375;
    const auto out0 = out.segments.size();

    if (fixedParts)
    {
        // the base plate: wide at the hoop, tapering to the heel, rounded corners, 0.35" thick
        slab ({ { -2.5, -0.1 }, { 2.0, -0.1 }, { 2.2, -0.4 }, { 2.2, -4.2 }, { 1.75, heelF - 0.6 }, { 1.55, heelF - 0.9 }, { -1.55, heelF - 0.9 },
                { -1.75, heelF - 0.6 }, { -2.5, -4.2 }, { -2.7, -0.4 } },
              0.35, 0.35, alu * 0.75, dim * 0.7);
        // the hoop clamp and its two rubber bumpers
        path ({ P (-0.9, -0.15, 0.35), P (-0.9, 0.1, 1.3), P (0.9, 0.1, 1.3), P (0.9, -0.15, 0.35) }, dim);
        for (double x : { -1.9, 1.9 })
            circle (P (x, -0.45, 0.6), F, Z, 0.25, dark, 12);
        // the post: a machined column (0.8" x 1.3") with its long slot, its foot on the plate
        const double px0 = postX - 0.4, px1 = postX + 0.4, pf0 = axleF + 0.75, pf1 = axleF - 0.55;
        for (double x : { px0, px1 })
        {
            path ({ P (x, pf0 + 0.3, 0.35), P (x, pf0, 1.0), P (x, pf0, axleZ - 0.5), P (x, pf1, axleZ - 0.9), P (x, pf1, 1.0), P (x, pf1 - 0.5, 0.35) }, alu);
            path ({ P (x, pf0 - 0.35, 2.2), P (x, pf0 - 0.35, axleZ - 1.6), P (x, pf1 + 0.3, axleZ - 1.7), P (x, pf1 + 0.3, 2.2) }, dim * 0.8, 1, true);
        }
        for (double f : { pf0, pf1 })
            seg (P (px0, f, axleZ - (f == pf0 ? 0.5 : 0.9)), P (px1, f, axleZ - (f == pf0 ? 0.5 : 0.9)), dim);
        // the axle's housing (a drum across the post's top), its knurled knob outside, the axle
        cylinder (P (postX - 0.55, axleF, axleZ), X, F, Z, 1.1, 0.85, alu, 24);
        cylinder (P (postX - 0.95, axleF, axleZ), X, F, Z, 0.4, 0.45, dim, 16);
        for (int k = 0; k < 12; ++k)
        {
            const double t = 2 * kPi * k / 12;
            const V3 e = 0.45 * (std::cos (t) * F + std::sin (t) * Z);
            seg (P (postX - 0.95, axleF, axleZ) + e, P (postX - 0.55, axleF, axleZ) + e, dim * 0.7);
        }
        seg (P (postX + 0.55, axleF, axleZ), P (camX + 0.3, axleF, axleZ), alu);
        // the hub's dial ring (black, with its scale)
        cylinder (P (-0.85, axleF, axleZ), X, F, Z, 0.55, 0.8, dark, 24);
        for (int k = 0; k < 18; ++k)
        {
            const double t = kPi * 0.2 + kPi * 0.9 * k / 17;
            const V3 e = std::cos (t) * F + std::sin (t) * Z;
            seg (P (-0.3, axleF, axleZ) + 0.8 * e, P (-0.3, axleF, axleZ) + 0.62 * e, dim * 0.6);
        }
        // the heel: the hinge rod across, its two end caps, the base's heel block
        seg (P (-bw - 0.2, heelF, heelZ - 0.1), P (bw + 0.2, heelF, heelZ - 0.1), dim);
        for (double x : { -bw - 0.2, bw + 0.2 })
            cylinder (P (x < 0 ? x - 0.3 : x, heelF, heelZ - 0.1), X, F, Z, 0.3, 0.28, alu, 12);
        slab ({ { -bw - 0.15, heelF + 0.4 }, { bw + 0.15, heelF + 0.4 }, { bw + 0.15, heelF - 0.5 }, { -bw - 0.15, heelF - 0.5 } }, 0.7, 0.35, dim, dark);
    }

    if (movingParts)
    {
        // the footboard: one flat plate, hinged at the heel; full width (2.75") to near the toe, then
        // a tongue on the right half, clear of the post, out to the toe link; 0.3" thick
        auto board = [&] (double x, double f) {
            const double t = (f - heelF) / (toeF - heelF);
            return P (x, f, heelZ + t * (toeZ - heelZ));
        };
        const double notchF = toeF - 1.6;
        const std::vector<std::pair<double, double>> outline { { -bw, heelF }, { -bw, notchF }, { 0.0, notchF }, { 0.0, toeF }, { bw, toeF }, { bw, heelF } };
        std::vector<V3> top, bottom;
        const V3 down { 0, 0, -0.3 };
        for (const auto& [x, f] : outline)
        {
            top.push_back (board (x, f));
            bottom.push_back (board (x, f) + down);
        }
        path (top, alu, 1, true);
        path (bottom, dim * 0.75, 1, true);
        for (size_t i = 0; i < top.size(); ++i)
            seg (top[i], bottom[i], dim * 0.75);
        // the two screws at the heel end, and a faint brushed line down the middle
        for (double x : { -0.45, 0.45 })
            circle (board (x, heelF + 0.75), X, normalised (board (0, toeF) - board (0, heelF)), 0.14, dim, 10);
        seg (board (0.0, heelF + 1.4), board (0.0, notchF - 0.6), dim * 0.45);
        // the toe link: a short bar from the tongue's end up to the chain
        const V3 toe = board (0.7, toeF + 0.15);
        const V3 link = toe + V3 { 0, 0, 0.9 } + 0.25 * F;
        for (double o : { -0.22, 0.22 })
            seg (toe + o * X, link + o * X, alu);
        circle (link, F, Z, 0.16, dim, 10);

        // the cam: an arc sprocket on the axle, the drummer's side, turning with the beater
        const double turn = swing * 0.55;
        const V3 cc = P (camX, axleF, axleZ);
        const double c0 = kPi * 0.55 + turn, c1 = kPi * 1.35 + turn;
        for (double o : { -0.18, 0.18 })
            arc (cc + o * X, F, Z, camR, alu, c0 - kPi, c1 - kPi, 20);
        for (double t : { c0 - kPi, c1 - kPi })
        {
            const V3 e = std::cos (t) * F + std::sin (t) * Z;
            seg (cc + 0.2 * e, cc + camR * e, alu * 0.85);
        }
        // the chain: over the cam's drummer side, down to the toe link
        const V3 chain0 = cc - camR * F;
        for (double o : { -0.14, 0.14 })
            seg (chain0 + o * X, link + o * X, alu);
        for (int k = 1; k < 16; ++k)
        {
            const V3 q = chain0 + (k / 16.0) * (link - chain0);
            seg (q - 0.14 * X, q + 0.14 * X, dim);
        }
        // the spring: a coil low on the plate at the front right, its lever up to the cam
        const V3 s0 = P (0.4, -0.95, 0.95), s1 = P (2.0, -0.95, 0.95) - (0.25 * swing) * X;
        {
            std::vector<V3> coil;
            const int turns = 9, n = turns * 10;
            for (int i = 0; i <= n; ++i)
            {
                const double t = (double) i / n, ang = 2 * kPi * turns * t;
                coil.push_back (s0 + t * (s1 - s0) + 0.3 * std::cos (ang) * F + 0.3 * std::sin (ang) * Z);
            }
            path (coil, alu * 0.85);
        }
        const V3 lev = P (camX + 0.35, axleF + 0.55 + 0.35 * swing, axleZ - 1.15 + 0.25 * swing);
        for (double o : { -0.15, 0.15 })
            seg (s1 + V3 { 0, 0, o }, lev + V3 { 0, 0, o }, alu * 0.9);
        circle (s1, F, Z, 0.2, dim, 10);

        // the beater: its clamp on the hub, the shaft (back toward the drummer at rest), the
        // disc beater with its holder
        const double rest = 40.0 * kPi / 180, hit = -9.0 * kPi / 180;
        const double th = rest + swing * (hit - rest);
        const V3 hub = P (0.25, axleF, axleZ);
        const V3 dir = -std::sin (th) * F + std::cos (th) * Z; // along the shaft, up
        const V3 face = std::cos (th) * F + std::sin (th) * Z; // the disc's face, toward the drum
        path ({ hub + 0.55 * dir - 0.35 * X - 0.45 * face, hub + 0.55 * dir + 0.35 * X - 0.45 * face, hub + 1.35 * dir + 0.35 * X - 0.45 * face,
                hub + 1.35 * dir - 0.35 * X - 0.45 * face },
              dark, 1, true);
        path ({ hub + 0.55 * dir - 0.35 * X + 0.35 * face, hub + 0.55 * dir + 0.35 * X + 0.35 * face, hub + 1.35 * dir + 0.35 * X + 0.35 * face,
                hub + 1.35 * dir - 0.35 * X + 0.35 * face },
              dark, 1, true);
        const V3 tip = hub + 8.4 * dir;
        for (double o : { -0.08, 0.08 })
            seg (hub + 1.0 * dir + o * X, tip + o * X, alu);
        // the felt beater (as before): a cylinder, its face toward the drum, its back a chrome cup;
        // the memory lock on the shaft
        circle (hub + 2.2 * dir, X, face, 0.22, dim, 10);
        const double fr = 0.9, fl = 0.75;
        const V3 front = tip + fl * face, cup = tip - 0.45 * face;
        circle (front, X, dir, fr, kHead * 0.95, 26);
        circle (tip, X, dir, fr, kHead * 0.76, 26);
        circle (cup, X, dir, fr * 0.7, alu * 0.8, 20);
        for (int k = 0; k < 4; ++k)
        {
            const double t = 2 * kPi * (k + 0.5) / 4;
            const V3 e = std::cos (t) * X + std::sin (t) * dir;
            seg (tip + fr * e, front + fr * e, kHead * 0.85);
            seg (cup + 0.7 * fr * e, tip + fr * e, alu * 0.7);
        }
    }

    // its screen bounds join the drawing's
    for (size_t i = out0; i < out.segments.size(); ++i)
    {
        const auto& sg = out.segments[i];
        out.x0 = std::min ({ out.x0, sg.a.x, sg.b.x });
        out.x1 = std::max ({ out.x1, sg.a.x, sg.b.x });
        out.y0 = std::min ({ out.y0, sg.a.y, sg.b.y });
        out.y1 = std::max ({ out.y1, sg.a.y, sg.b.y });
    }
}

// =====================================================================================
void HeadMesh::draw (const Hit* hits, int numHits, std::vector<Segment>& out, std::vector<Flash>& flashes) const
{
    constexpr int NR = kRings, NT = kPoints;
    const auto& shapes = modeShapes();
    static thread_local std::vector<double> disp, glow;
    static thread_local std::vector<Pt> sp;
    disp.assign ((size_t) (NR * NT), 0.0);
    glow.assign (disp.size(), 0.0);
    sp.resize (disp.size());

    // the cos and sin of m theta for the mesh's angles
    static const auto trig = [] {
        std::array<std::array<double, kPoints>, 10> t {};
        for (int m = 0; m < 5; ++m)
            for (int j = 0; j < kPoints; ++j)
            {
                const double th = 2 * kPi * j / kPoints;
                t[(size_t) (2 * m)][(size_t) j] = std::cos (m * th);
                t[(size_t) (2 * m + 1)][(size_t) j] = std::sin (m * th);
            }
        return t;
    }();

    bool moving = false;
    for (int h = 0; h < numHits; ++h)
    {
        const Hit& hit = hits[h];
        for (size_t k = 0; k < modes().size(); ++k)
        {
            const auto& md = modes()[k];
            const double w = std::abs (besselJ (md.m, md.z * hit.rpos)) * std::exp (-(md.m + 1.6 * md.n) / (1.5 + 4 * hit.vel));
            if (w < 0.04)
                continue;
            const double tau = 0.15 + 0.6 / (1 + md.m + 2 * md.n);
            const double amp = hit.vel * w * std::exp (-hit.age / tau) * (1 - std::exp (-hit.age / 0.01))
                               * std::cos (2 * kPi * (1.5 + 0.7 * (md.m + md.n)) * hit.age);
            if (std::abs (amp) < 1e-5)
                continue;
            moving = true;
            // cos (m (theta - angle)) = cos m theta cos m angle + sin m theta sin m angle
            const double ca = std::cos (md.m * hit.angle), sa = std::sin (md.m * hit.angle);
            const auto& c = trig[(size_t) (2 * md.m)];
            const auto& s = trig[(size_t) (2 * md.m + 1)];
            for (int i = 0; i < NR; ++i)
            {
                const double ai = amp * shapes.j[k][(size_t) i];
                double* row = disp.data() + i * NT;
                for (int j = 0; j < NT; ++j)
                    row[j] += ai * (c[(size_t) j] * ca + s[(size_t) j] * sa);
            }
        }
    }
    if (moving)
    {
        double mx = 0;
        for (double x : disp)
            mx = std::max (mx, std::abs (x));
        const double scale = std::pow (std::min (1.0, mx / 0.3), 0.5);
        for (size_t k = 0; k < disp.size(); ++k)
            glow[k] = mx > 1e-9 ? std::pow (std::abs (disp[k]) / mx, 1.6) * scale : 0.0;
        const double k = 0.35 * r / 7;
        for (size_t q = 0; q < disp.size(); ++q)
            sp[q] = cam.project (rest[q] + (k * disp[q]) * a);
    }
    else
        sp = restScreen;

    float ytop = 1e9f, ybot = -1e9f;
    for (const Pt& p : sp)
    {
        ytop = std::min (ytop, p.y);
        ybot = std::max (ybot, p.y);
    }
    const double span = std::max (1.0f, ybot - ytop);
    const double b0 = clear ? 0.14 : 0.22, b1 = clear ? 0.16 : 0.34;
    auto col = [&] (size_t p, size_t q) {
        const double ytl = 1 - (0.5 * (sp[p].y + sp[q].y) - ytop) / span;
        const Rgb base = kHead * ((b0 + b1 * ytl) * bright);
        const double gg = 0.5 * (glow[p] + glow[q]);
        return base + (kHot - base) * gg;
    };
    // the rings drawn (every ringStep-th, and always the edge) and the points on them
    static thread_local std::vector<int> drawn;
    drawn.clear();
    for (int i = 0; i < NR - 1; i += ringStep)
        drawn.push_back (i);
    drawn.push_back (NR - 1);
    for (int i : drawn)
        for (int j = 0; j < NT; j += pointStep)
        {
            const size_t p = (size_t) (i * NT + j), q = (size_t) (i * NT + (j + pointStep) % NT);
            out.push_back ({ sp[p], sp[q], col (p, q), 1, false });
        }
    for (int j = 0; j < NT; j += 2 * pointStep)
        for (size_t k = 0; k + 1 < drawn.size(); ++k)
        {
            const size_t p = (size_t) (drawn[k] * NT + j), q = (size_t) (drawn[k + 1] * NT + j);
            out.push_back ({ sp[p], sp[q], col (p, q), 1, false });
        }

    // the flash where the stick landed
    for (int h = 0; h < numHits; ++h)
    {
        const Hit& hit = hits[h];
        const double T = 0.25 + 0.25 * hit.vel;
        if (hit.age >= T)
            continue;
        const V3 q = C + (r * hit.rpos) * (std::cos (hit.angle) * u + std::sin (hit.angle) * v);
        const double kk = (1 - hit.age / T) * (1 - hit.age / T);
        const double rad = (8 + 18 * hit.vel) * (1 - 0.3 * hit.age / T);
        flashes.push_back ({ cam.project (q), (float) rad, (float) (kk * (0.4 + 0.6 * hit.vel)) });
    }
}

// =====================================================================================
const DrumLook& drumLook (int drum)
{
    // KitMap.h order. The kit as Kris described it (2026-11-24): the Popcorn Snare is a 10" snare;
    // Floor Tom 1 (tom5) a 16" floor tom; the gong drum TAMA's 22" head on a 20" x 14" shell. Tom 1 is 10" x 10"
    // and the octobans 17.5", 18.5", 21" and 23.5" long (Kris, 2026-11-25). The other depths as
    // drawn in the approved mockup.
    static const std::array<DrumLook, kDrumCount> table { {
        { Kind::kick, 22, 18, 10 },  { Kind::kick, 22, 18, 10 },
        { Kind::snare, 14, 6.5, 10 }, { Kind::snare, 10, 5, 8 },
        { Kind::tom, 10, 10, 6 },    { Kind::tom, 12, 9, 6 },      { Kind::tom, 13, 10, 6 },     { Kind::tom, 14, 11, 6 },
        { Kind::floor, 16, 16, 8 },  { Kind::floor, 18, 16, 8 },
        { Kind::gong, 22, 14, 10, 20 },
        { Kind::octo, 6, 17.5, 4 },  { Kind::octo, 6, 18.5, 4 },   { Kind::octo, 6, 21, 4 },     { Kind::octo, 6, 23.5, 4 },
    } };
    return table[(size_t) std::clamp (drum, 0, kDrumCount - 1)];
}

namespace
{
// A drum of the plugin at the origin and the DRUM page's camera, at F = 1000 and not yet centred.
// The camera aims at the middle of the drum as drawn by default (depthOffIn 0).
void pageDrum (int drum, DrumSpec& spec, Camera& cam, double depthOffIn)
{
    const auto& look = drumLook (drum);
    spec.kind = look.kind;
    spec.C = { 0, 0, 0 };
    // a bass drum lies on its side, its batter head toward the drummer, turned to show its
    // shell; the gong drum tilts left and toward the drummer, seen from low down, as on its stand
    // in Kris's photo (mirrored, his choice 2026-11-25)
    // each bass drum turned as it sits in the kit's V (Kris, 2026-11-27): Kick 1, on the left, shows its
    // shell to the left; Kick 2, on the right, to the right (the view drawn for both until then)
    const double side = drum == 0 ? -1.0 : 1.0;
    spec.axis = look.kind == Kind::kick ? V3 { -0.5 * side, -0.866, 0 } : look.kind == Kind::gong ? V3 { -0.8, -0.75, 1 } : V3 { 0, 0, 1 };
    spec.r = (look.shellDiameter > 0 ? look.shellDiameter : look.diameter) / 2;
    spec.headR = look.shellDiameter > 0 ? look.diameter / 2 : 0.0;
    spec.d = look.depth + depthOffIn;
    spec.lugs = look.lugs;
    V3 a, u, v;
    basis (spec.axis, a, u, v);
    const V3 target = spec.C - (0.5 * look.depth) * a;
    // the mockup's camera; a bass drum, lying on its side, and the gong drum seen from lower down
    cam = Camera::orbit (target, look.kind == Kind::kick ? 14 : look.kind == Kind::gong ? 10 : 34, 70, 1000, 0, 0, look.kind == Kind::kick ? -8 * side : -8);
}

// The deepest a drum's shell is drawn (Size: Toms 1-4 up to +2"), inches more than by default.
double deepestOffIn (int drum)
{
    return drum >= 6 && drum <= 9 ? 2.0 : 0.0; // KitMap.h kDepthUpIn on the drums whose depth plays
}

// The scale (F = 1000 x k) that fits a drum at its deepest into maxW x maxH.
double fitScale (int drum, double maxW, double maxH)
{
    DrumSpec s;
    Camera c;
    pageDrum (drum, s, c, deepestOffIn (drum));
    const DrumDrawing p = drawOutline (s, c);
    return std::min (maxW / std::max (1.0, (double) (p.x1 - p.x0)), maxH / std::max (1.0, (double) (p.y1 - p.y0)));
}
} // namespace

Framed frameDrum (int drum, Pt centre, double maxW, double maxH, double depthOffIn, Scale scale)
{
    Framed fr;
    Camera cam;
    pageDrum (drum, fr.spec, cam, depthOffIn);
    // measure at F = 1000, then scale and centre (the projection is linear in F, cx and cy)
    const DrumDrawing probe = drawOutline (fr.spec, cam);
    double k = 0;
    if (scale == Scale::kit)
    {
        // every drum at the scale that fits the largest (worked out once per box)
        static std::mutex lock;
        static std::vector<std::array<double, 3>> known; // maxW, maxH, k
        std::lock_guard<std::mutex> g (lock);
        for (const auto& e : known)
            if (e[0] == maxW && e[1] == maxH)
                k = e[2];
        if (k <= 0)
        {
            k = 1e9;
            for (int d = 0; d < kDrumCount; ++d)
                k = std::min (k, fitScale (d, maxW, maxH));
            known.push_back ({ maxW, maxH, k });
        }
    }
    else
        k = fitScale (drum, maxW, maxH);
    cam.F = 1000 * k;
    cam.cx = centre.x - k * 0.5 * (probe.x0 + probe.x1);
    cam.cy = centre.y - k * 0.5 * (probe.y0 + probe.y1);
    fr.cam = cam;
    return fr;
}

const std::vector<KitItem>& kitLayout()
{
    static const std::vector<KitItem> items = [] {
        std::vector<KitItem> it;
        auto lean = [] (double f, double s) { return V3 { s, -f, 1.0 }; };
        const double vk = 20 * kPi / 180;
        // the kicks in a V; the rack toms over them, in front of the kick shells they sit on
        it.push_back ({ 0, { Kind::kick, { -13.8, 7, 11.5 }, { std::sin (vk), -std::cos (vk), 0 }, 11, 18, 10 }, 0 });
        it.push_back ({ 1, { Kind::kick, { 13.8, 7, 11.5 }, { -std::sin (vk), -std::cos (vk), 0 }, 11, 18, 10 }, 0 });
        it.push_back ({ 2, { Kind::snare, { 0, -14, 25 }, lean (0.1, 0), 7, 6.5, 10 }, 0 });          // the Snare
        it.push_back ({ 3, { Kind::snare, { -27, -12, 25 }, lean (0.12, 0.08), 5, 5.0, 8 }, 0 });     // the Popcorn Snare, 10"
        it.push_back ({ 4, { Kind::tom, { -24.5, 14.5, 33.5 }, lean (0.45, 0.16), 5, 10, 6 }, -25 }); // head level with the others
        it.push_back ({ 5, { Kind::tom, { -9.5, 16, 34.5 }, lean (0.45, 0.05), 6, 9, 6 }, -25 });
        it.push_back ({ 6, { Kind::tom, { 6.5, 16, 35.5 }, lean (0.45, -0.05), 6.5, 10, 6 }, -25 });
        it.push_back ({ 7, { Kind::tom, { 23, 14.5, 36.5 }, lean (0.45, -0.16), 7, 11, 6 }, -25 });
        // the floor toms side by side, coming toward the camera, turned slightly to the snare
        it.push_back ({ 8, { Kind::floor, { 27.5, -1, 20.5 }, { -0.12, -0.08, 1.0 }, 8, 16, 8 }, 0 });
        it.push_back ({ 9, { Kind::floor, { 37.5, -18, 21.5 }, { -0.14, -0.06, 1.0 }, 9, 16, 8 }, 0 });
        // the gong drum on its stand right of the floor toms, tilted to the drummer (Kris's markup,
        // mockup approved 2026-11-26), its head turned 35 degrees toward the toms about the
        // vertical, the tilt kept (Kris, 2026-11-27)
        const double gt = 35 * kPi / 180;
        it.push_back ({ 10, { Kind::gong, { 54, -6, 32 }, { -0.75 * std::cos (gt) - 0.6 * std::sin (gt), 0.75 * std::sin (gt) - 0.6 * std::cos (gt), 1.0 },
                              10, 14, 10, 11 }, 0 });
        return it;
    }();
    return items;
}

DrumSpec kitSpec (const KitItem& item, double depthOffIn)
{
    DrumSpec s = item.spec;
    if (depthOffIn != 0)
    {
        V3 a, u, v;
        basis (s.axis, a, u, v);
        s.d += depthOffIn;
        // a floor tom stands on its legs with its head at playing height: its bottom moves
        // (Kris, 2026-11-28); a rack tom rests on the kick: its head moves
        if (item.spec.kind != Kind::floor)
            s.C = s.C + depthOffIn * a;
    }
    return s;
}

Camera kitCamera()
{
    // Kris, 2026-11-27: 95 % of the approved size (focal length 1480), 70 right and 30 down of the
    // page's centre (centre 680, 575), so the kit sits right of the title and clear of the logo
    return Camera ({ 11, -128, 58 }, { 11, 4, 22 }, 1480 * 0.95, 750, 605);
}

int kitItemForDrum (int drum)
{
    const auto& items = kitLayout();
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].drum == drum)
            return (int) i;
    return -1;
}

} // namespace tympanis::ui::wire
