// tympanis_render — render a sequence of hits through the C++ engine (no JUCE).
//
//   tympanis_render --bake resources/bake --seq seq.json --out out.wav [--raw out.f64] [--one-call]
//
// --one-call renders each stretch between hits in one piece, as the prototype does (port check).
//
// seq.json:
//   { "sample_rate": 44100, "duration": 4.0, "variation": true, "noiseless": false,
//     "restrike_damp": true, "snare_buzz": 0.5,
//     "drums": { "snare1": { "tune_semi": 0, "lug_detune": 0, "vel_range_db": 24 }, ... },
//     "events": [ [time_s, "snare1", vel, "head"], ... ] }
//
// Events are rendered the way the prototype's Kit.sequence renders them: the kit is rendered up
// to each event's sample, then the hit is struck.
#include "Engine.h"
#include "Json.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace tympanis;

namespace
{
void writeWav16(const std::string& path, const std::vector<double>& x, int sr)
{
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write((const char*) &v, 4); };
    auto u16 = [&](uint16_t v) { f.write((const char*) &v, 2); };
    const uint32_t bytes = (uint32_t) x.size() * 2;
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32((uint32_t) sr); u32((uint32_t) sr * 2); u16(2); u16(16);
    f.write("data", 4); u32(bytes);
    for (double v : x)
    {
        const double c = std::max(-1.0, std::min(1.0, v));
        const int16_t s = (int16_t) std::lrint(c * 32767.0);
        f.write((const char*) &s, 2);
    }
}

Art parseArt(const std::string& s)
{
    if (s == "rim") return Art::Rim;
    if (s == "side") return Art::Side;
    return Art::Head;
}
} // namespace

int main(int argc, char** argv)
{
    std::string bake = "resources/bake", seqPath, out, raw;
    double gain = 1.0;
    bool oneCall = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--bake") bake = next();
        else if (a == "--seq") seqPath = next();
        else if (a == "--out") out = next();
        else if (a == "--raw") raw = next();
        else if (a == "--gain") gain = std::stod(next());
        else if (a == "--one-call") oneCall = true;
        else { std::cerr << "unknown argument " << a << "\n"; return 2; }
    }
    if (seqPath.empty()) { std::cerr << "usage: tympanis_render --bake DIR --seq SEQ.json [--out X.wav] [--raw X.f64] [--gain G]\n"; return 2; }
    try
    {
        const KitData data = loadKitFromDirectory(bake);
        const json::Value seq = json::parseFile(seqPath);
        auto num = [&](const char* k, double def) { const json::Value* v = seq.find(k); return v ? v->number() : def; };
        auto flag = [&](const char* k, bool def) { const json::Value* v = seq.find(k); return v ? v->boolean() : def; };
        const int sr = (int) num("sample_rate", 44100);
        const double dur = num("duration", 4.0);
        Kit kit;
        const int chunk = oneCall ? (int) ((dur + 1) * sr) / 32 * 32 + 32 : Kit::kChunk;
        kit.init(&data, flag("restrike_damp", true), flag("variation", true), chunk);
        kit.prepare(sr);
        kit.setNoiseless(flag("noiseless", false));
        kit.setSnareBuzz(num("snare_buzz", 0.5));
        std::map<std::string, int> idx;
        for (int i = 0; i < kit.numDrums(); ++i) idx[data.drums[(size_t) i].key] = i;
        if (const json::Value* ds = seq.find("drums"))
            for (const auto& [key, p] : ds->object())
            {
                Drum& d = kit.drum(idx.at(key));
                for (const auto& [name, v] : p.object())
                {
                    const double x = v.number();
                    if (name == "tune_semi") d.setTune(x);
                    else if (name == "lug_detune") d.setLugDetune(x);
                    else if (name == "decay") d.setDecay(x);
                    else if (name == "glide_amt") d.setGlideAmt(x);
                    else if (name == "click_mix") d.setClickMix(x);
                    else if (name == "noise_mix") d.setNoiseMix(x);
                    else if (name == "vel_range_db") d.setVelRangeDb(x);
                    else if (name == "strike_pos") d.setStrikePos(x);
                    else if (name == "size_in") d.setSize(x);
                    else if (name == "tube_len") d.setTubeLen(x);
                    else throw std::runtime_error("unknown drum parameter " + name);
                }
            }
        struct Ev { int64_t n; int drum; double vel; Art art; };
        std::vector<Ev> ev;
        for (const auto& e : seq.at("events").array())
        {
            const auto& a = e.array();
            ev.push_back({ (int64_t) (a[0].number() * sr), idx.at(a[1].string()), a[2].number(), a.size() > 3 ? parseArt(a[3].string()) : Art::Head });
        }
        std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.n < b.n; });
        const int64_t N = (int64_t) (dur * sr);
        std::vector<double> y((size_t) N, 0.0);
        int64_t cur = 0;
        size_t ei = 0;
        const auto t0 = std::chrono::steady_clock::now();
        while (cur < N)
        {
            const int64_t nxt = ei < ev.size() ? std::min(ev[ei].n, N) : N;
            if (nxt > cur) { kit.render(y.data() + cur, (int) (nxt - cur)); cur = nxt; }
            while (ei < ev.size() && ev[ei].n <= cur) { kit.strike(ev[ei].drum, ev[ei].vel, ev[ei].art); ++ei; }
            if (ei >= ev.size() && cur >= N) break;
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "rendered %.2f s in %.3f s (%.1fx real time)\n", dur, secs, dur / std::max(secs, 1e-9));
        for (auto& v : y) v *= gain;
        if (! raw.empty()) { std::ofstream f(raw, std::ios::binary); f.write((const char*) y.data(), (std::streamsize) (y.size() * sizeof(double))); }
        if (! out.empty()) writeWav16(out, y, sr);
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
