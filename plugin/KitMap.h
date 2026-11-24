// KitMap.h — the drums (host parameters) and the note rows (MIDI map) of the plugin.
//
// Every articulation is its own row with its own MIDI note, played on its drum's one shared
// resonator bank (snares: head, rim shot, side stick; toms: head, rim shot; kicks, gong drum and
// octobans: head). Notes and the MIDI channel are session state, not host parameters.
//
// The default notes are a PLACEHOLDER (GM-like where GM has a matching sound, the rest on free
// notes away from GM's cymbal notes). The layout is still to be decided by Kris. A note belongs
// to at most one row (Kris, 2026-11-24): giving a row a note another row has clears that row.
#pragma once

#include "dsp/KitData.h"

#include <array>

namespace tympanis
{

struct DrumInfo
{
    const char* key;     // bake key (resources/bake/<key>.bake.json) and parameter ID prefix
    const char* name;    // display name
    double sizeIn;       // catalog head diameter (inches): the Size parameter's default; range 0.5x .. 2x
    bool strikePos;      // has a Strike Position parameter (not the bass drums: fixed beater, as sampled)
};

// Same order as tools/bake_kit.py KIT (the engine's drum index). Sizes as prototype/engine.py NATIVE_IN.
constexpr std::array<DrumInfo, 17> kDrums { {
    { "kick1", "Kick 1", 22, false },   { "kick2", "Kick 2", 22, false },
    { "snare1", "Snare 1", 14, true }, { "snare2", "Snare 2", 14, true }, { "snare3", "Snare 3", 14, true }, { "snare4", "Snare 4", 14, true },
    { "tom1", "Tom 1", 10, true },     { "tom2", "Tom 2", 12, true },     { "tom3", "Tom 3", 13, true },     { "tom4", "Tom 4", 14, true },
    { "tom5", "Tom 5", 16, true },     { "tom6", "Tom 6", 18, true },
    { "gong", "Gong Drum", 22, true },
    { "octo1", "Octoban 1", 6, true }, { "octo2", "Octoban 2", 6, true }, { "octo3", "Octoban 3", 6, true }, { "octo4", "Octoban 4", 6, true },
} };

struct NoteRow
{
    const char* id;      // stable ID for the saved state
    const char* name;
    int drum;            // index into kDrums
    Art art;
    int defaultNote;     // placeholder, see above
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
