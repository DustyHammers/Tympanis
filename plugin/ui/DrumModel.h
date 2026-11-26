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
// A small head (the octobans' 6") draws every other ring and point, so its lines stay apart.
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
    int ringStep = 1, pointStep = 1;
    double near = 0;
    std::vector<V3> rest; // kRings x kPoints
    std::vector<Pt> restScreen;
};

// A drum as the mockup's Drum: C the batter head's centre (the gong drum: its shell's top
// edge), the axis out of the batter head, radius r and depth d of the shell (inches), lugs per
// head. headR: the gong drum's head radius, larger than its shell's (0: the shell's).
struct DrumSpec
{
    Kind kind;
    V3 C;
    V3 axis;
    double r, d;
    int lugs;
    double headR = 0;
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

// A bass drum pedal (the KIT page; the shape of Kris's photos of Trick Big Foot / Black Widow
// pedals, 2026-11-25), added to a drawing: clamped to a kick's batter hoop on the floor, the
// footboard toward the drummer. Fixed parts: the base plate (wide at the hoop, tapering to the
// heel), the hoop clamp and bumpers, the single post on the drummer's left with the axle's
// housing and knurled knob, the hub's dial ring, the heel hinge and its end caps. Moving parts:
// the flat footboard (13" x 2.75": full width, then a tongue on the right clear of the post; no
// logo), the toe link, the arc cam, the chain, the low spring and its lever, the beater (clamp,
// shaft, memory lock, the felt beater of the first mockup). swing: 0 at rest (beater back, toe
// up), 1 against the head (toe down).
enum class PedalParts { fixed, moving, all };
void addKickPedal (DrumDrawing& out, const DrumSpec& kick, const Camera& cam, double sel = 0.0, double swing = 0.0,
                   PedalParts parts = PedalParts::all);
// The beater's swing a time after a kick's hit: against the head at the hit, back at rest by 0.2 s.
double pedalSwing (double secondsSinceHit);

// ---- The drums of the plugin (KitMap.h order) as drawn on the DRUM and TUNING pages, and the kit.

// The plugin's drums (KitMap.h kDrums.size(); the editor checks they agree).
constexpr int kDrumCount = 15;

struct DrumLook
{
    Kind kind;
    double diameter, depth; // inches, as drawn and as the spec line under the DRUM box says
    int lugs;
    double shellDiameter = 0; // the gong drum's shell, under its larger head (0: the diameter)
};
const DrumLook& drumLook (int drum);

// The DRUM page's (and the TUNING page's) single drum: the drum at the origin, seen by the
// mockup's camera (34 degrees up, 70" away, turned 8 degrees), scaled to fit `maxW` x `maxH`
// design units and centred on `centre`. The gong drum is tilted toward the camera, as on its
// stand. depthOffIn: the shell this much deeper (or shallower) than drawn by default (Size on
// Toms 1-4; the camera stays on the default drum's middle). Scale: kit — one scale for every drum
// (the DRUM page, Kris 2026-11-25: the drums proportional to each other), the one that fits the
// largest with the toms at their deepest; own — this drum's, at its deepest (the TUNING page's
// small drum), so a change of depth shows as a longer or shorter shell.
struct Framed
{
    DrumSpec spec;
    Camera cam;
};
enum class Scale { kit, own };
Framed frameDrum (int drum, Pt centre, double maxW, double maxH, double depthOffIn = 0, Scale scale = Scale::own);

// The KIT page: the drums drawn (the gong drum and the octobans are not drawn for now), in the
// approved layout.
struct KitItem
{
    int drum;       // KitMap.h index
    DrumSpec spec;
    double depthBias; // the rack toms sit in front of the kicks they rest on
};
const std::vector<KitItem>& kitLayout();
// A kit item with its shell depthOffIn deeper (a rack tom: its bottom stays where it rests on the
// kick; the head moves along the axis, down when shallower; Kris, 2026-11-25).
DrumSpec kitSpec (const KitItem& item, double depthOffIn);
Camera kitCamera();
// The kit item a drum is drawn as, or -1.
int kitItemForDrum (int drum);

} // namespace tympanis::ui::wire
