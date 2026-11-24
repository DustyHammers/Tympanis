// KitMap.h — the drums (host parameters) and the note rows (MIDI map) of the plugin.
//
// Every articulation is its own row with its own MIDI note, played on its drum's one shared
// resonator bank (snares: head, rim shot, side stick; toms: head, rim shot; kicks, gong drum and
// octobans: head). Notes and the MIDI channel are session state, not host parameters.
//
// The default notes are a PLACEHOLDER (GM-like where GM has a matching sound, the rest on free
// notes away from GM's cymbal notes). The layout is still to be decided by Kris.
#pragma once

#include "dsp/KitData.h"

#include <array>

namespace tympanis
{

struct DrumInfo
{
    const char* key;     // bake key (resources/bake/<key>.bake.json) and parameter ID prefix
    const char* name;    // display name
};

// Same order as tools/bake_kit.py KIT (the engine's drum index).
constexpr std::array<DrumInfo, 17> kDrums { {
    { "kick1", "Kick 1" },   { "kick2", "Kick 2" },
    { "snare1", "Snare 1" }, { "snare2", "Snare 2" }, { "snare3", "Snare 3" }, { "snare4", "Snare 4" },
    { "tom1", "Tom 1" },     { "tom2", "Tom 2" },     { "tom3", "Tom 3" },     { "tom4", "Tom 4" },
    { "tom5", "Tom 5" },     { "tom6", "Tom 6" },
    { "gong", "Gong Drum" },
    { "octo1", "Octoban 1" }, { "octo2", "Octoban 2" }, { "octo3", "Octoban 3" }, { "octo4", "Octoban 4" },
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

constexpr int kDefaultMidiChannel = 10;   // placeholder (Cymbalis defaults to 16); to be decided
constexpr int kNoteNone = -1;

} // namespace tympanis
