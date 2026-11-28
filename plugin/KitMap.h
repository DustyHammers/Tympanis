// KitMap.h — the drums (host parameters) and the note rows (MIDI map) of the plugin.
//
// Every articulation is its own row with its own MIDI note, played on its drum's one shared
// resonator bank (snares: head, rim shot, side stick; toms: head, rim shot; kicks, gong drum and
// octobans: head). Notes and the MIDI channel are session state, not host parameters.
//
// The Snare and the Popcorn Snare each have two models, wires on and wires off, behind their
// Snares On/Off switch (Kris, 2026-11-26): the switch picks which one a new hit plays, and every
// other setting is shared. The Snare's are Snare 1's and Snare 2's models (each exactly as
// before); the Popcorn Snare's are Snare 4's and its derived wires-off model (tools/snares_off.py).
// Snare 3 is out of the plugin; its model is kept (tools/bake_kit.py RESERVED).
//
// The default notes follow General MIDI (Kris, 2026-11-24, "default to GM for now"): GM's bass
// drums, snare, side stick and six toms where GM has the sound (the Snare's rim shot on GM's
// electric snare, 40, as most GM-compatible drum maps); the rows GM has no sound for on notes
// below GM's range (24..32) and on GM's bongo/conga and whistle/guiro/claves/wood-block notes
// (60..64, 72..77), away from its cymbals. A note belongs to at most one row (Kris, 2026-11-24):
// giving a row a note another row has clears that row.
#pragma once

#include "dsp/KitData.h"

#include <array>
#include <string_view>

namespace tympanis
{

// The engine's drums: the baked kit, in tools/bake_kit.py KIT order (the engine's drum index;
// resources/bake/kit.bake.json "drums"), with each one's catalog head diameter as baked.
struct EngineDrum
{
    const char* key;     // bake key (resources/bake/<key>.bake.json)
    double sizeIn;       // the bake's native_in
};
constexpr std::array<EngineDrum, 17> kEngineDrums { {
    { "kick1", 22 }, { "kick2", 22 }, { "snare1", 14 }, { "snare2", 14 }, { "snare4", 10 }, { "snare4_off", 10 },
    { "tom1", 10 }, { "tom2", 12 }, { "tom3", 13 }, { "tom4", 14 }, { "tom5", 16 }, { "tom6", 18 },
    { "gong", 22 }, { "octo1", 6 }, { "octo2", 6 }, { "octo3", 6 }, { "octo4", 6 },
} };

struct DrumInfo
{
    const char* key;     // parameter ID prefix and saved-state key (the wires-on model's bake key)
    const char* name;    // display name
    double sizeIn;       // catalog head diameter (inches), as baked (the bake's native_in)
    bool strikePos;      // has a Strike Position parameter (not the bass drums: fixed beater, as sampled)
    double depthIn;      // shell depth (inches), as drawn: the Size parameter's default
    bool depth;          // Size (the shell's depth) plays: Toms 1-4 and the floor toms; greyed out on the others
    int engine;          // its engine drum (index into kEngineDrums); the wires-on model on the snares
    int engineOff;       // the wires-off model behind the Snares switch, or -1 (no switch)
};

// The Size parameter is the shell's depth (Kris, 2026-11-25): its default -4 .. +2 inches, in 1" steps.
// The floor toms only go shallower (Kris, 2026-11-28), and further: their 16" down to 10", the same
// depth ratio (1.6) as a rack tom at its shallowest, so it changes them as much (option 1).
constexpr double kDepthDownIn = 4, kDepthUpIn = 2;

// The drums the plugin shows: their parameters, rows and drawings. Sizes as prototype/engine.py
// NATIVE_IN, except the Popcorn Snare (Snare 4), a 10" snare (Kris, 2026-11-24; tools/bake_kit.py
// CATALOG_IN). Depths as drawn (plugin/ui/DrumModel.cpp drumLook, which must agree).
constexpr std::array<DrumInfo, 15> kDrums { {
    { "kick1", "Kick 1", 22, false, 18, false, 0, -1 },         { "kick2", "Kick 2", 22, false, 18, false, 1, -1 },
    { "snare1", "Snare", 14, true, 6.5, false, 2, 3 },           { "snare4", "Popcorn Snare", 10, true, 5, false, 4, 5 },
    { "tom1", "Tom 1", 10, true, 10, true, 6, -1 },              { "tom2", "Tom 2", 12, true, 9, true, 7, -1 },
    { "tom3", "Tom 3", 13, true, 10, true, 8, -1 },              { "tom4", "Tom 4", 14, true, 11, true, 9, -1 },
    { "tom5", "Floor Tom 1", 16, true, 16, true, 10, -1 },       { "tom6", "Floor Tom 2", 18, true, 16, true, 11, -1 },
    { "gong", "Gong Drum", 22, true, 14, false, 12, -1 },
    { "octo1", "Octoban 1", 6, true, 17.5, false, 13, -1 },     { "octo2", "Octoban 2", 6, true, 18.5, false, 14, -1 },
    { "octo3", "Octoban 3", 6, true, 21, false, 15, -1 },       { "octo4", "Octoban 4", 6, true, 23.5, false, 16, -1 },
} };

// How much shallower and deeper than drawn Size goes: the floor toms stand on their legs and only
// go shallower, 6" (16" to 10").
inline bool isFloorTom(const DrumInfo& d)
{
    return std::string_view(d.key) == "tom5" || std::string_view(d.key) == "tom6";
}
inline double depthDownIn(const DrumInfo& d) { return isFloorTom(d) ? 6.0 : kDepthDownIn; }
inline double depthUpIn(const DrumInfo& d) { return isFloorTom(d) ? 0.0 : kDepthUpIn; }

struct NoteRow
{
    const char* id;      // stable ID for the saved state
    const char* name;
    int drum;            // index into kDrums
    Art art;
    int defaultNote;     // General MIDI, see above
};

constexpr std::array<NoteRow, 25> kRows { {
    { "kick1",       "Kick 1",              0, Art::Head, 36 },
    { "kick2",       "Kick 2",              1, Art::Head, 35 },
    { "snare1",      "Snare",                    2, Art::Head, 38 },
    { "snare1_rim",  "Snare Rim Shot",           2, Art::Rim,  40 },
    { "snare1_side", "Snare Side Stick",         2, Art::Side, 37 },
    { "snare4",      "Popcorn Snare",            3, Art::Head, 30 },
    { "snare4_rim",  "Popcorn Snare Rim Shot",   3, Art::Rim,  31 },
    { "snare4_side", "Popcorn Snare Side Stick", 3, Art::Side, 32 },
    { "tom1",        "Tom 1",                    4, Art::Head, 50 },
    { "tom1_rim",    "Tom 1 Rim Shot",           4, Art::Rim,  72 },
    { "tom2",        "Tom 2",                    5, Art::Head, 48 },
    { "tom2_rim",    "Tom 2 Rim Shot",           5, Art::Rim,  73 },
    { "tom3",        "Tom 3",                    6, Art::Head, 47 },
    { "tom3_rim",    "Tom 3 Rim Shot",           6, Art::Rim,  74 },
    { "tom4",        "Tom 4",                    7, Art::Head, 45 },
    { "tom4_rim",    "Tom 4 Rim Shot",           7, Art::Rim,  75 },
    { "tom5",        "Floor Tom 1",              8, Art::Head, 43 },
    { "tom5_rim",    "Floor Tom 1 Rim Shot",     8, Art::Rim,  76 },
    { "tom6",        "Floor Tom 2",              9, Art::Head, 41 },
    { "tom6_rim",    "Floor Tom 2 Rim Shot",     9, Art::Rim,  77 },
    { "gong",        "Gong Drum",               10, Art::Head, 64 },
    { "octo1",       "Octoban 1",               11, Art::Head, 60 },
    { "octo2",       "Octoban 2",               12, Art::Head, 61 },
    { "octo3",       "Octoban 3",               13, Art::Head, 62 },
    { "octo4",       "Octoban 4",               14, Art::Head, 63 },
} };

// Each drum's default Pan: where it sits from the player's seat at the Snare (Kris, 2026-11-27, render
// 58): measured on the KIT page's layout (the sideways share of each drum's direction), then by ear:
// the bass drums just off centre, the sides brought in, the octobans by the Popcorn Snare. -1 left,
// +1 right; L/R as the PAN knob reads them.
struct DrumPan
{
    const char* drum;   // a kDrums key
    float pan;
};
constexpr std::array<DrumPan, 15> kDefaultPans { {
    { "kick1", -0.15f }, { "kick2", 0.15f }, { "snare1", 0.0f }, { "snare4", -0.68f },
    { "tom1", -0.44f }, { "tom2", -0.19f }, { "tom3", 0.10f }, { "tom4", 0.39f },
    { "tom5", 0.58f }, { "tom6", 0.64f }, { "gong", 0.67f },
    { "octo1", -0.74f }, { "octo2", -0.70f }, { "octo3", -0.66f }, { "octo4", -0.62f },
} };

inline float defaultPan(const char* drum)
{
    for (const auto& p : kDefaultPans)
        if (std::string_view(p.drum) == drum) return p.pan;
    return 0.0f;
}

// Level trims (dB) for the rows whose full-velocity hits peak far above the rest (Kris,
// 2026-11-24): with the output gain raised 6 dB to Cymbalis's loudness, each is turned down so
// its loudest hit at velocity 127 (worst of 32) peaks at or below -3 dBFS at the default
// settings, rounded down to 0.5 dB. The octobans are 4 dB down so their Level has room to come
// up (Kris, 2026-11-27: Level 0 dB, the default, is now 4 dB under their level before). Every
// other row: 0 dB.
struct RowTrim
{
    const char* row;   // a kRows id
    double db;
};
constexpr std::array<RowTrim, 8> kRowTrims { {
    { "tom2_rim", -5.5 },   // -3.7 dBFS before, +2.3 with the 6 dB
    { "tom3_rim", -8.0 },   // -1.4 dBFS before, +4.6 with the 6 dB
    { "tom5_rim", -3.5 },   // -6.8 dBFS before, -0.7 with the 6 dB; -3.5 (was -2.5) with its pan at R 58 (2026-11-27)
    { "tom6_rim", -3.5 },   // -5.6 dBFS before, +0.4 with the 6 dB
    { "octo1", -4.0 },      // the octobans (Kris, 2026-11-27)
    { "octo2", -4.0 },
    { "octo3", -4.0 },
    { "octo4", -4.0 },
} };

constexpr int kDefaultMidiChannel = 10;   // Kris, 2026-11-24
constexpr int kNoteNone = -1;

} // namespace tympanis
