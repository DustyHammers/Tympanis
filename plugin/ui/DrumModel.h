#pragma once

// The wireframe drums (the approved mockup, docs/ui/tympanis_mockup.py, ported line for line):
// geometry, camera, colours and the head's motion after hits. Plain C++, no JUCE: WireView
// rasterises what this produces.
//
// Units: inches in the world (x to the right, y away from the drummer, z up); the screen is in
// design units of the 1536 x 1024 artwork. Colours are RGB 0..255 and may go above 255 (the
// bloom looks for lines brighter than 140; the final image clips at 255), as in the mockup.

#include <array>
#include <cstdint>
#include <vector>

namespace tympanis::ui::wire
{

struct V3
{
    double x = 0, y = 0, z = 0;
};
inline V3 operator+ (V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V3 operator- (V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V3 operator* (V3 a, double k) { return { a.x * k, a.y * k, a.z * k }; }
inline V3 operator* (double k, V3 a) { return a * k; }
double dot (V3 a, V3 b);
V3 cross (V3 a, V3 b);
double length (V3 a);
V3 normalised (V3 a);

struct Rgb
{
    float r = 0, g = 0, b = 0;
};
inline Rgb operator+ (Rgb a, Rgb b) { return { a.r + b.r, a.g + b.g, a.b + b.b }; }
inline Rgb operator- (Rgb a, Rgb b) { return { a.r - b.r, a.g - b.g, a.b - b.b }; }
inline Rgb operator* (Rgb a, double k) { return { (float) (a.r * k), (float) (a.g * k), (float) (a.b * k) }; }

struct Pt
{
    float x = 0, y = 0;
};

// The mockup's Cam: a pinhole camera at `pos` looking at `look`, z up; focal length F (design
// units) and the screen position cx, cy of the line of sight.
struct Camera
{
    Camera() = default;
    Camera (V3 pos, V3 look, double F, double cx, double cy);
    // The mockup's orbit(): at `dist` from `target`, `elevDeg` above it, turned `azDeg` about z
    // (0: looking along +y).
    static Camera orbit (V3 target, double elevDeg, double dist, double F, double cx, double cy, double azDeg);

    Pt project (V3 p) const noexcept;
    double depth (V3 p) const noexcept { return dot (p - pos, f); }

    V3 pos, f, r, u;
    double F = 1000, cx = 0, cy = 0;
};

enum class Kind : std::uint8_t
{
    tom,
    floor,
    snare,
    kick,
    octo,
    gong
};

// One line segment on the screen. Side-layer segments (hardware just past the drum's
// silhouette) show only outside the drum's body hull.
struct Segment
{
    Pt a, b;
    Rgb c;
    float width; // design units: 1 or 2
    bool side;
};

// A hit on a head, for the head's motion and flash.
struct Hit
{
    double age;   // seconds since the strike
    double vel;   // 0..1
    double rpos;  // 0 centre .. 1 edge
    double angle; // radians, about the axis, from u
};

// The batter head's mesh (25 rings x 96 points), which flexes in its membrane modes after hits.
class HeadMesh
{
public:
    static constexpr int kRings = 25, kPoints = 96;

    // Lines of the mesh for these hits (none: at rest) into `out`; and the flash spots, as
    // centre (design units), radius and strength.
    struct Flash
    {
        Pt centre;
        float radius, strength;
    };
    void draw (const Hit* hits, int numHits, std::vector<Segment>& out, std::vector<Flash>& flashes) const;

    // The angle (about the axis, from u) of the head's point nearest the camera.
    double nearAngle() const noexcept { return near; }
    // Screen bounds of the mesh at rest, design units.
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    // On screen, the flex moves a point at most this far (design units) per unit of displacement.
    float flexReach = 0;

private:
    friend struct DrumBuilder;
    Camera cam;
    V3 C, a, u, v;
    double r = 1;
    bool clear = false;
    double bright = 1;
    double near = 0;
    std::vector<V3> rest; // kRings x kPoints
    std::vector<Pt> restScreen;
};

// A drum as the mockup's Drum: C the batter head's centre, the axis out of the batter head,
// radius r and depth d (inches), lugs per head.
struct DrumSpec
{
    Kind kind;
    V3 C;
    V3 axis;
    double r, d;
    int lugs;
};

// A drum drawn by a camera: its lines (without the batter head's mesh), the hull of its body
// (masks the side layer) and of its silhouette (hides what is behind it on the KIT page), and
// the head mesh if the batter head faces the camera.
struct DrumDrawing
{
    std::vector<Segment> segments;
    std::vector<Pt> bodyHull, silhouette;
    bool headVisible = false;
    HeadMesh head;
    double depth = 0; // the KIT page's drawing order: far first
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // screen bounds of everything, design units
};

// sel: 1 for the selected drum on the KIT page (35 % brighter), else 0.
DrumDrawing drawDrum (const DrumSpec& spec, const Camera& cam, double sel = 0.0);

// ---- The drums of the plugin (KitMap.h order) as drawn on the DRUM and TUNING pages, and the kit.

struct DrumLook
{
    Kind kind;
    double diameter, depth; // inches, as drawn and as the spec line under the DRUM box says
    int lugs;
};
const DrumLook& drumLook (int drum);

// The DRUM page's (and the TUNING page's) single drum: the drum at the origin, seen by the
// mockup's camera (34 degrees up, 70" away, turned 8 degrees), scaled to fit `maxW` x `maxH`
// design units and centred on `centre`.
struct Framed
{
    DrumSpec spec;
    Camera cam;
};
Framed frameDrum (int drum, Pt centre, double maxW, double maxH);

// The KIT page: the drums drawn (Snare 1 stands for Snare 1 and 2; Snare 3, the gong drum and
// the octobans are not drawn for now), in the approved layout.
struct KitItem
{
    int drum;       // KitMap.h index
    DrumSpec spec;
    double depthBias; // the rack toms sit in front of the kicks they rest on
};
const std::vector<KitItem>& kitLayout();
Camera kitCamera();
// The kit item a drum is drawn as (Snare 2 as Snare 1), or -1.
int kitItemForDrum (int drum);

} // namespace tympanis::ui::wire
