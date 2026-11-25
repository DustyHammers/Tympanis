// KitMap.h — the drums (host parameters) and the note rows (MIDI map) of the plugin.
//
// Every articulation is its own row with its own MIDI note, played on its drum's one shared
// resonator bank (snares: head, rim shot, side stick; toms: head, rim shot; kicks, gong drum and
// octobans: head). Notes and the MIDI channel are session state, not host parameters.
//
// The default notes follow General MIDI (Kris, 2026-11-24, "default to GM for now"): GM's bass
// drums, snare, side stick and six toms where GM has the sound (Snare 1's rim shot on GM's
// electric snare, 40, as most GM-compatible drum maps); the rows GM has no sound for on notes
// below GM's range (24..32) and on GM's bongo/conga and whistle/guiro/claves/wood-block notes
// (60..64, 72..77), away from its cymbals. A note belongs to at most one row (Kris, 2026-11-24):
// giving a row a note another row has clears that row.
#pragma once

#include "dsp/KitData.h"

#include <array>

namespace tympanis
{

struct DrumInfo
{
    const char* key;     // bake key (resources/bake/<key>.bake.json) and parameter ID prefix
    const char* name;    // display name
    double sizeIn;       // catalog head diameter (inches), as baked (the bake's native_in)
    bool strikePos;      // has a Strike Position parameter (not the bass drums: fixed beater, as sampled)
    double depthIn;      // shell depth (inches), as drawn: the Size parameter's default
    bool depth;          // Size (the shell's depth) plays: Toms 1-4 only; greyed out on the others
};

// The Size parameter is the shell's depth (Kris, 2026-11-25): its default -4 .. +2 inches, in 1" steps.
constexpr double kDepthDownIn = 4, kDepthUpIn = 2;

// Same order as tools/bake_kit.py KIT (the engine's drum index). Sizes as prototype/engine.py NATIVE_IN,
// except Snare 4, a 10" snare (Kris, 2026-11-24; tools/bake_kit.py CATALOG_IN). Depths as drawn
// (plugin/ui/DrumModel.cpp drumLook, which must agree).
constexpr std::array<DrumInfo, 17> kDrums { {
    { "kick1", "Kick 1", 22, false, 18, false },   { "kick2", "Kick 2", 22, false, 18, false },
    { "snare1", "Snare 1", 14, true, 6.5, false }, { "snare2", "Snare 2", 14, true, 6.5, false },
    { "snare3", "Snare 3", 14, true, 6.5, false }, { "snare4", "Snare 4", 10, true, 5, false },
    { "tom1", "Tom 1", 10, true, 8, true },        { "tom2", "Tom 2", 12, true, 9, true },
    { "tom3", "Tom 3", 13, true, 10, true },       { "tom4", "Tom 4", 14, true, 11, true },
    { "tom5", "Tom 5", 16, true, 16, false },      { "tom6", "Tom 6", 18, true, 16, false },
    { "gong", "Gong Drum", 22, true, 14, false },
    { "octo1", "Octoban 1", 6, true, 11, false },  { "octo2", "Octoban 2", 6, true, 14.7, false },
    { "octo3", "Octoban 3", 6, true, 18.3, false }, { "octo4", "Octoban 4", 6, true, 22, false },
} };

struct NoteRow
{
    const char* id;      // stable ID for the saved state
    const char* name;
    int drum;            // index into kDrums
    Art art;
    int defaultNote;     // General MIDI, see above
};

constexpr std::array<NoteRow, 31> kRows { {
    { "kick1",       "Kick 1",              0, Art::Head, 36 },
    { "kick2",       "Kick 2",              1, Art::Head, 35 },
    { "snare1",      "Snare 1",             2, Art::Head, 38 },
    { "snare1_rim",  "Snare 1 Rim Shot",    2, Art::Rim,  40 },
    { "snare1_side", "Snare 1 Side Stick",  2, Art::Side, 37 },
    { "snare2",      "Snare 2",             3, Art::Head, 24 },
    { "snare2_rim",  "Snare 2 Rim Shot",    3, Art::Rim,  25 },
    { "snare2_side", "Snare 2 Side Stick",  3, Art::Side, 26 },
    { "snare3",      "Snare 3",             4, Art::Head, 27 },
    { "snare3_rim",  "Snare 3 Rim Shot",    4, Art::Rim,  28 },
    { "snare3_side", "Snare 3 Side Stick",  4, Art::Side, 29 },
    { "snare4",      "Snare 4",             5, Art::Head, 30 },
    { "snare4_rim",  "Snare 4 Rim Shot",    5, Art::Rim,  31 },
    { "snare4_side", "Snare 4 Side Stick",  5, Art::Side, 32 },
    { "tom1",        "Tom 1",               6, Art::Head, 50 },
    { "tom1_rim",    "Tom 1 Rim Shot",      6, Art::Rim,  72 },
    { "tom2",        "Tom 2",               7, Art::Head, 48 },
    { "tom2_rim",    "Tom 2 Rim Shot",      7, Art::Rim,  73 },
    { "tom3",        "Tom 3",               8, Art::Head, 47 },
    { "tom3_rim",    "Tom 3 Rim Shot",      8, Art::Rim,  74 },
    { "tom4",        "Tom 4",               9, Art::Head, 45 },
    { "tom4_rim",    "Tom 4 Rim Shot",      9, Art::Rim,  75 },
    { "tom5",        "Tom 5",              10, Art::Head, 43 },
    { "tom5_rim",    "Tom 5 Rim Shot",     10, Art::Rim,  76 },
    { "tom6",        "Tom 6",              11, Art::Head, 41 },
    { "tom6_rim",    "Tom 6 Rim Shot",     11, Art::Rim,  77 },
    { "gong",        "Gong Drum",          12, Art::Head, 64 },
    { "octo1",       "Octoban 1",          13, Art::Head, 60 },
    { "octo2",       "Octoban 2",          14, Art::Head, 61 },
    { "octo3",       "Octoban 3",          15, Art::Head, 62 },
    { "octo4",       "Octoban 4",          16, Art::Head, 63 },
} };

constexpr int kDefaultMidiChannel = 10;   // Kris, 2026-11-24
constexpr int kNoteNone = -1;

} // namespace tympanis
