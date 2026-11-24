#include "DrumModel.h"

#include <algorithm>
#include <cmath>

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
constexpr Rgb kAmber { 64, 158, 80 };      // British racing green lacquer, the body
constexpr Rgb kAmberHi { 128, 232, 150 };  // emerald where the light catches it
constexpr Rgb kBurstD { 30, 60, 28 };      // near-black green at the edges
constexpr Rgb kChrome { 220, 216, 214 };   // hoops, lugs, claws
constexpr Rgb kChromeD { 156, 152, 150 };
constexpr Rgb kHead { 222, 204, 178 };     // coated head, pale cream
constexpr Rgb kHot { 255, 238, 215 };

const V3 kLight = normalised ({ 0.2, -0.5, 0.85 });

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
    V3 C, a, u, v;
    double r, d;
    DrumDrawing out;

    DrumBuilder (const DrumSpec& spec, const Camera& camera, double sel) : s (spec), cam (camera), bright (1.0 + 0.35 * sel)
    {
        C = s.C;
        basis (s.axis, a, u, v);
        r = s.r;
        d = s.d;
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

    void shell()
    {
        const int NS = s.kind == Kind::octo ? 36 : 72;
        const int nseg = std::max (6, (int) (d * 1.2));
        const double cov = s.kind != Kind::kick ? 0.45 : 0.0;
        const bool two = s.kind != Kind::octo && s.kind != Kind::gong;
        const double x0 = cov / d, x1 = 1 - (two ? cov / d : 0.0);
        std::vector<Pt> sp ((size_t) ((NS + 1) * (nseg + 1)));
        std::vector<char> facing ((size_t) NS + 1);
        std::vector<double> lam ((size_t) NS + 1), side ((size_t) NS + 1);
        for (int i = 0; i <= NS; ++i)
        {
            const double t = 2 * kPi * i / NS;
            const V3 n = normalAt (t);
            for (int k = 0; k <= nseg; ++k)
            {
                const double x = x0 + (x1 - x0) * k / nseg;
                sp[(size_t) (i * (nseg + 1) + k)] = cam.project (C - (d * x) * a + r * n);
            }
            facing[(size_t) i] = vis (C - 0.5 * d * a + r * n, n);
            lam[(size_t) i] = std::clamp (dot (n, kLight), 0.0, 1.0);
            side[(size_t) i] = std::abs (dot (n, cam.r));
        }
        auto shellCol = [&] (int i, double x) {
            const double burst = std::pow (std::max (0.0, 1 - std::abs (x - 0.45) / 0.75), 0.8);
            Rgb c = kBurstD + (kAmber - kBurstD) * burst;
            const double l = lam[(size_t) i];
            c = c + (kAmberHi - c) * (std::max (0.0, l - 0.55) * 1.4 * burst);
            return c * ((0.7 + 0.65 * l) * (1 - 0.35 * std::pow (side[(size_t) i], 4)) * bright);
        };
        auto at = [&] (int i, int k) { return sp[(size_t) (i * (nseg + 1) + k)]; };
        for (int i = 0; i < NS; ++i)
        {
            if (! (facing[(size_t) i] && facing[(size_t) i + 1]))
                continue;
            for (int k = 0; k < nseg; ++k)
            {
                line2 (at (i, k), at (i, k + 1), shellCol (i, (k + 0.5) / nseg));
                if (k > 0)
                    line2 (at (i, k), at (i + 1, k), shellCol (i, (double) k / nseg) * 0.7);
            }
        }
        // the shell's two end edges (the batter edge all round when the head faces the camera)
        for (int e = 0; e < 2; ++e)
        {
            const double off = e == 0 ? 0.0 : -d, kcol = e == 0 ? 0.8 : 0.55;
            const Rgb c = (kBurstD * (1.4 * kcol) + kAmber * 0.35) * bright;
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

        if (s.kind == Kind::kick)
        {
            const Rgb wood = kBurstD * 0.9 + kAmber * 0.75;
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
                k = std::max (k, std::pow (std::clamp (1 - (dd / 0.95) * (dd / 0.95), 0.0, 1.0), 1.5));
            }
            ew[(size_t) i] = k;
        }
        auto hoopLine = [&] (double dr0, double drEw, double h0, double hEw, double off, double sgn, Rgb col, float th, bool allRound) {
            Pt prev {};
            V3 prevP {};
            for (int i = 0; i < NA; ++i)
            {
                const double t = tt[(size_t) i];
                const V3 n = normalAt (t);
                const V3 P = C + (off + sgn * (h0 + hEw * ew[(size_t) i])) * a + (r + dr0 + drEw * ew[(size_t) i]) * n;
                const Pt sp = cam.project (P);
                if (i > 0 && (allRound || vis (prevP, normalAt (tt[(size_t) i - 1]))))
                    line2 (prev, sp, col * bright, th);
                prev = sp;
                prevP = P;
            }
        };
        for (auto [off, sgn] : ends)
        {
            const bool face = vis (C + off * a, sgn * a);
            hoopLine (0.03, 0, 0.52, 0, off, sgn, kChrome * 0.85, 1, face);          // inner tier: inner edge
            hoopLine (0.13, 0, 0.58, 0, off, sgn, kChrome * 1.0, 2, face);           // inner tier: rounded top
            hoopLine (0.24, 0, 0.50, 0, off, sgn, kChrome * 0.85, 1, face);          // inner tier: outer edge
            hoopLine (0.27, 0, 0.20, 0, off, sgn, kChrome * 0.55, 1, face);          // the step down
            hoopLine (0.32, 0, 0.16, 0, off, sgn, kChrome * 0.8, 1, face);           // outer tier: top, inner
            hoopLine (0.58, 0.30, 0.14, 0, off, sgn, kChrome * 1.0, 2, face);        // outer tier: top edge
            hoopLine (0.64, 0.30, -0.10, 0, off, sgn, kChrome * 0.55, 1, false);     // outer tier: face
            hoopLine (0.64, 0.34, -0.42, -0.30, off, sgn, kChrome * 0.9, 1, false);  // lower edge into the ears
            for (double t : lugT)
            {
                V3 n, tg, ax;
                frame (t, sgn, n, tg, ax);
                const V3 e0 = C + off * a + (r + 0.62) * n;
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
                // the lug, and the rod from under the ear into its flat end
                const double y0 = s.kind == Kind::octo ? 0.7 : 0.45;
                const double Lg = s.kind == Kind::octo ? 1.3 : std::min (1.7, 0.9 + 0.1 * d);
                V3 end;
                if (bulletLug (t, off, sgn, y0, Lg, end))
                    line (rc - 0.7 * ax, end, kChromeD * (0.95 * bright), 1, sideLayer);
            }
        }
        if (s.kind == Kind::octo)
            ring (0.0, 0.0, -d, 1, kBurstD * 1.5 + kAmber * 0.4, 1, vis (C - d * a, -1.0 * a));
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

    void snareParts()
    {
        // the strainer on the shell's side, and the butt end opposite
        for (auto [t, h] : { std::pair<double, double> { -0.55 * kPi, 2.2 }, { 0.45 * kPi, 1.4 } })
        {
            const V3 n = normalAt (t), tg = -std::sin (t) * u + std::cos (t) * v;
            const V3 P = C - 0.5 * d * a + r * n;
            if (! vis (P, n))
                continue;
            poly ({ P + 0.8 * tg + (h / 2) * a + 0.5 * n, P - 0.8 * tg + (h / 2) * a + 0.5 * n, P - 0.8 * tg - (h / 2) * a + 0.5 * n,
                    P + 0.8 * tg - (h / 2) * a + 0.5 * n },
                  kChrome * bright, 1, false, true);
        }
    }

    std::vector<Pt> hull (double dr, double front, double back) const
    {
        std::vector<Pt> pts;
        for (int i = 0; i < 64; ++i)
        {
            const double t = 2 * kPi * i / 63;
            pts.push_back (cam.project (ringPoint (r + dr, front, t)));
            pts.push_back (cam.project (ringPoint (r + dr, back, t)));
        }
        return convexHull (std::move (pts));
    }

    void headMesh()
    {
        out.headVisible = vis (C, a);
        HeadMesh& h = out.head;
        h.cam = cam;
        h.C = C;
        h.a = a;
        h.u = u;
        h.v = v;
        h.r = r;
        h.clear = s.kind == Kind::kick;
        h.bright = bright;
        const auto& rr = ringRadii();
        h.rest.resize ((size_t) (HeadMesh::kRings * HeadMesh::kPoints));
        h.restScreen.resize (h.rest.size());
        h.x0 = h.y0 = 1e9f;
        h.x1 = h.y1 = -1e9f;
        for (int i = 0; i < HeadMesh::kRings; ++i)
            for (int j = 0; j < HeadMesh::kPoints; ++j)
            {
                const double t = 2 * kPi * j / HeadMesh::kPoints;
                const V3 P = C + r * rr[(size_t) i] * normalAt (t);
                const size_t k = (size_t) (i * HeadMesh::kPoints + j);
                h.rest[k] = P;
                h.restScreen[k] = cam.project (P);
                h.x0 = std::min (h.x0, h.restScreen[k].x);
                h.x1 = std::max (h.x1, h.restScreen[k].x);
                h.y0 = std::min (h.y0, h.restScreen[k].y);
                h.y1 = std::max (h.y1, h.restScreen[k].y);
            }
        // how far the flex can move a point on the screen, per unit of displacement
        const double k = 0.35 * r / 7;
        float reach = 0;
        for (int j = 0; j < HeadMesh::kPoints; j += 8)
        {
            const V3 P = h.rest[(size_t) (12 * HeadMesh::kPoints + j)];
            const Pt p0 = cam.project (P), p1 = cam.project (P + k * a);
            reach = std::max (reach, std::hypot (p1.x - p0.x, p1.y - p0.y));
        }
        h.flexReach = reach;
        // the head's point nearest the camera: where a drummer's stick lands
        const V3 toCam = cam.pos - C;
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
        out.bodyHull = hull (0.25, 0.55, -d - 0.55);
        headMesh();
        if (s.kind == Kind::snare)
            snareParts();
        out.silhouette = hull (0.9, 0.6, -d - 0.6);
        out.depth = cam.depth (C - 0.5 * d * a);
        bounds();
        return std::move (out);
    }
};

DrumDrawing drawDrum (const DrumSpec& spec, const Camera& cam, double sel)
{
    return DrumBuilder (spec, cam, sel).build();
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
    for (int i = 0; i < NR; ++i)
        for (int j = 0; j < NT; ++j)
        {
            const size_t p = (size_t) (i * NT + j), q = (size_t) (i * NT + (j + 1) % NT);
            out.push_back ({ sp[p], sp[q], col (p, q), 1, false });
        }
    for (int j = 0; j < NT; j += 2)
        for (int i = 0; i < NR - 1; ++i)
        {
            const size_t p = (size_t) (i * NT + j), q = (size_t) ((i + 1) * NT + j);
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
    // KitMap.h order. The kit as Kris described it (2026-11-24): Snare 4 is a 10" snare; Tom 5
    // a 16" floor tom. Depths as drawn in the approved mockup; Snare 3, the gong drum and the
    // octobans (not on the KIT page yet) at the sizes the DRUM page draws them.
    static const std::array<DrumLook, 17> table { {
        { Kind::kick, 22, 18, 10 },  { Kind::kick, 22, 18, 10 },
        { Kind::snare, 14, 6.5, 10 }, { Kind::snare, 14, 6.5, 10 }, { Kind::snare, 14, 6.5, 10 }, { Kind::snare, 10, 5, 8 },
        { Kind::tom, 10, 8, 6 },     { Kind::tom, 12, 9, 6 },      { Kind::tom, 13, 10, 6 },     { Kind::tom, 14, 11, 6 },
        { Kind::floor, 16, 16, 8 },  { Kind::floor, 18, 16, 8 },
        { Kind::gong, 22, 10, 10 },
        { Kind::octo, 6, 14, 4 },    { Kind::octo, 6, 14, 4 },     { Kind::octo, 6, 14, 4 },     { Kind::octo, 6, 14, 4 },
    } };
    return table[(size_t) std::clamp (drum, 0, 16)];
}

Framed frameDrum (int drum, Pt centre, double maxW, double maxH)
{
    const auto& look = drumLook (drum);
    Framed fr;
    fr.spec.kind = look.kind;
    fr.spec.C = { 0, 0, 0 };
    // a bass drum lies on its side, its batter head toward the drummer, turned to show its shell
    fr.spec.axis = look.kind == Kind::kick ? V3 { -0.5, -0.866, 0 } : V3 { 0, 0, 1 };
    fr.spec.r = look.diameter / 2;
    fr.spec.d = look.depth;
    fr.spec.lugs = look.lugs;
    V3 a, u, v;
    basis (fr.spec.axis, a, u, v);
    const V3 target = fr.spec.C - (0.5 * fr.spec.d) * a;
    // measure at F = 1000, then scale and centre (the projection is linear in F, cx and cy)
    // the mockup's camera; a bass drum, lying on its side, seen from lower down
    Camera cam = Camera::orbit (target, look.kind == Kind::kick ? 14 : 34, 70, 1000, 0, 0, -8);
    const DrumDrawing probe = drawDrum (fr.spec, cam);
    const double w = probe.x1 - probe.x0, h = probe.y1 - probe.y0;
    const double k = std::min (maxW / std::max (1.0, w), maxH / std::max (1.0, h));
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
        it.push_back ({ 2, { Kind::snare, { 0, -14, 25 }, lean (0.1, 0), 7, 6.5, 10 }, 0 });          // Snare 1 and 2
        it.push_back ({ 5, { Kind::snare, { -27, -12, 25 }, lean (0.12, 0.08), 5, 5.0, 8 }, 0 });     // Snare 4, 10"
        it.push_back ({ 6, { Kind::tom, { -24.5, 14.5, 33.5 }, lean (0.45, 0.16), 5, 8, 6 }, -25 });
        it.push_back ({ 7, { Kind::tom, { -9.5, 16, 34.5 }, lean (0.45, 0.05), 6, 9, 6 }, -25 });
        it.push_back ({ 8, { Kind::tom, { 6.5, 16, 35.5 }, lean (0.45, -0.05), 6.5, 10, 6 }, -25 });
        it.push_back ({ 9, { Kind::tom, { 23, 14.5, 36.5 }, lean (0.45, -0.16), 7, 11, 6 }, -25 });
        // the floor toms side by side, coming toward the camera, turned slightly to the snare
        it.push_back ({ 10, { Kind::floor, { 27.5, -1, 20.5 }, { -0.12, -0.08, 1.0 }, 8, 16, 8 }, 0 });
        it.push_back ({ 11, { Kind::floor, { 37.5, -18, 21.5 }, { -0.14, -0.06, 1.0 }, 9, 16, 8 }, 0 });
        return it;
    }();
    return items;
}

Camera kitCamera()
{
    return Camera ({ 11, -128, 58 }, { 11, 4, 22 }, 1480, 800, 575);
}

int kitItemForDrum (int drum)
{
    if (drum == 3)
        drum = 2; // Snare 2 is Snare 1 with its wires off
    const auto& items = kitLayout();
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].drum == drum)
            return (int) i;
    return -1;
}

} // namespace tympanis::ui::wire
