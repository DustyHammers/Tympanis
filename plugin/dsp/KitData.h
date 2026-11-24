// KitData.h — the baked kit (resources/bake/*.bake.json, written by tools/bake_kit.py).
//
// Everything here is read-only after loading. The engine (Engine.h) keeps its own run-time state.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace tympanis
{

enum class Art { Head = 0, Rim = 1, Side = 2 };
constexpr int kNumArts = 3;
const char* artName(Art a);

enum class DrumKind { Kick, Snare, Tom, Octoban };

struct Layer
{
    std::vector<double> freq, decay, amp, phase;
    std::vector<double> glideW;            // empty: every slot glides fully (weight 1)
    double glideG = 0, glideTau = 0.05, glideG2 = 0, glideTau2 = 1.0;
    std::vector<double> noisePeak, noiseDecay;              // per noise band
    std::vector<std::array<double, 4>> trModes;             // modelled contact transient: f, d, a, phase
    std::vector<std::array<double, 3>> trPulses;            // Ricker pulses: centre time, peak freq, amp
    std::vector<std::array<double, 3>> trBurst;             // per burst band: peak, time to peak, decay
};

struct LugTable
{
    int count = 0;
    std::vector<int> slots;                 // slots that get a twin oscillator
    std::vector<double> rel, phi;           // per twin: |eps_2m| / 4, eigen-angle
    std::vector<int> m;                     // per twin: angular order
    double theta = 0, sigma = 0.05;         // strike angle relative to the lugs; tension error at lug_detune 1
};

struct SymBank                              // snares: the head modes the other drums drive (snare buzz)
{
    std::vector<double> freq, decay, w, noisePeak;
    double href = 1;
};

struct DrumData
{
    std::string key, model;
    DrumKind kind = DrumKind::Tom;
    int K = 0;
    double nativeIn = 14, fMain = 100, fref = 200, r0 = 0.35, glideGmax = 0;
    std::vector<std::array<double, 2>> bands;          // noise bands lo, hi
    std::vector<double> bandNormCorr;
    std::array<std::vector<Layer>, kNumArts> arts;     // empty vector: articulation not available
    LugTable lug;
    std::vector<unsigned char> tubeCluster;            // octobans: air-column (main pitch) slots
    double gridStep = 0.01;
    std::vector<std::vector<double>> strikeGainGrid;   // [radius index][slot], head; empty for kicks
    std::vector<std::vector<double>> shape2Grid;       // [radius index][slot]
    bool hasSym = false;
    SymBank sym;

    bool hasArt(Art a) const { return ! arts[(size_t) a].empty(); }
};

struct KitData
{
    std::vector<DrumData> drums;
    std::vector<std::array<double, 2>> burstBands;
    std::vector<double> burstNormCorr;
};

// Loaders. Throw std::runtime_error (or json::ParseError) on malformed data.
DrumData parseDrum(const std::string& jsonText);
void parseKitHeader(const std::string& jsonText, KitData& kit, std::vector<std::string>& drumKeys);
KitData loadKitFromDirectory(const std::string& bakeDir);

} // namespace tympanis
