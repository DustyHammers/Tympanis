#include "KitData.h"
#include "Json.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace tympanis
{

const char* artName(Art a)
{
    switch (a)
    {
        case Art::Head: return "head";
        case Art::Rim: return "rim";
        case Art::Side: return "side";
    }
    return "?";
}

namespace
{
using json::Value;

std::vector<double> nums(const Value& v)
{
    std::vector<double> out;
    out.reserve(v.array().size());
    for (const auto& x : v.array()) out.push_back(x.number());
    return out;
}

template <size_t N>
std::vector<std::array<double, N>> rows(const Value& v)
{
    std::vector<std::array<double, N>> out;
    for (const auto& r : v.array())
    {
        const auto& a = r.array();
        if (a.size() != N) throw std::runtime_error("bake: row has the wrong length");
        std::array<double, N> row {};
        for (size_t i = 0; i < N; ++i) row[i] = a[i].number();
        out.push_back(row);
    }
    return out;
}

std::vector<std::vector<double>> grid(const Value& v)
{
    std::vector<std::vector<double>> out;
    for (const auto& r : v.array()) out.push_back(nums(r));
    return out;
}

Layer parseLayer(const Value& l, int K, size_t nb)
{
    Layer L;
    L.freq = nums(l.at("freq"));
    L.decay = nums(l.at("decay"));
    L.amp = nums(l.at("amp"));
    L.phase = nums(l.at("phase"));
    if (! l.at("glide_w").isNull()) L.glideW = nums(l.at("glide_w"));
    L.glideG = l.at("glide_g").number();
    L.glideTau = l.at("glide_tau").number();
    L.glideG2 = l.at("glide_g2").number();
    L.glideTau2 = l.at("glide_tau2").number();
    L.noisePeak = nums(l.at("noise_peak"));
    L.noiseDecay = nums(l.at("noise_decay"));
    L.trModes = rows<4>(l.at("tr_modes"));
    L.trPulses = rows<3>(l.at("tr_pulses"));
    L.trBurst = rows<3>(l.at("tr_burst"));
    const size_t k = (size_t) K;
    if (L.freq.size() != k || L.decay.size() != k || L.amp.size() != k || L.phase.size() != k
        || (! L.glideW.empty() && L.glideW.size() != k))
        throw std::runtime_error("bake: layer slot count differs from K");
    if (L.noisePeak.size() != nb || L.noiseDecay.size() != nb)
        throw std::runtime_error("bake: layer noise band count differs");
    return L;
}

std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (! f) throw std::runtime_error("cannot open " + path);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
} // namespace

DrumData parseDrum(const std::string& text)
{
    return parseDrum(json::parse(text));
}

DrumData parseDrum(const json::Value& v)
{
    DrumData d;
    d.key = v.at("key").string();
    d.model = v.at("model").string();
    d.K = (int) v.at("K").number();
    const std::string kind = v.at("kind").string();
    d.kind = kind == "kick" ? DrumKind::Kick : kind == "snare" ? DrumKind::Snare : kind == "octoban" ? DrumKind::Octoban : DrumKind::Tom;
    d.nativeIn = v.at("native_in").number();
    d.fMain = v.at("f_main").number();
    d.fref = v.at("fref").number();
    d.r0 = v.at("r0").number();
    d.glideGmax = v.at("glide_gmax").number();
    d.bands = rows<2>(v.at("bands"));
    d.bandNormCorr = nums(v.at("band_norm_corr"));
    if (d.bandNormCorr.size() != d.bands.size()) throw std::runtime_error("bake: band_norm_corr size");
    for (const auto& [name, layers] : v.at("articulations").object())
    {
        int a = name == "head" ? 0 : name == "rim" ? 1 : name == "side" ? 2 : -1;
        if (a < 0) throw std::runtime_error("bake: unknown articulation " + name);
        for (const auto& l : layers.array()) d.arts[(size_t) a].push_back(parseLayer(l, d.K, d.bands.size()));
    }
    if (d.arts[0].empty()) throw std::runtime_error("bake: no head articulation");
    const Value& lug = v.at("lug");
    d.lug.count = (int) lug.at("count").number();
    for (const auto& s : lug.at("slots").array()) d.lug.slots.push_back((int) s.number());
    d.lug.rel = nums(lug.at("rel"));
    d.lug.phi = nums(lug.at("phi"));
    for (const auto& s : lug.at("m").array()) d.lug.m.push_back((int) s.number());
    d.lug.theta = lug.at("theta").number();
    d.lug.sigma = lug.at("sigma").number();
    if (! v.at("tube_cluster").isNull())
        for (const auto& s : v.at("tube_cluster").array()) d.tubeCluster.push_back((unsigned char) (s.number() != 0));
    d.gridStep = v.at("grid_step").number();
    for (const auto& [name, g] : v.at("strike_gain_grid").object())
    {
        const int a = name == "head" ? 0 : name == "rim" ? 1 : name == "side" ? 2 : -1;
        if (a < 0) throw std::runtime_error("bake: unknown articulation in strike_gain_grid " + name);
        d.strikeGainGrid[(size_t) a] = grid(g);
    }
    d.shape2Grid = grid(v.at("shape2_grid"));
    for (const auto& g : d.strikeGainGrid)
        for (const auto& r : g) if ((int) r.size() != d.K) throw std::runtime_error("bake: strike grid row size");
    for (const auto& r : d.shape2Grid) if ((int) r.size() != d.K) throw std::runtime_error("bake: shape grid row size");
    if (d.shape2Grid.empty()) throw std::runtime_error("bake: no shape2 grid");
    if (const Value* s = v.find("sym"))
    {
        d.hasSym = true;
        d.sym.freq = nums(s->at("freq"));
        d.sym.decay = nums(s->at("decay"));
        d.sym.w = nums(s->at("w"));
        d.sym.href = s->at("href").number();
        d.sym.noisePeak = nums(s->at("noise_peak"));
    }
    return d;
}

void parseKitHeader(const std::string& text, KitData& kit, std::vector<std::string>& drumKeys)
{
    parseKitHeader(json::parse(text), kit, drumKeys);
}

void parseKitHeader(const json::Value& v, KitData& kit, std::vector<std::string>& drumKeys)
{
    drumKeys.clear();
    for (const auto& k : v.at("drums").array()) drumKeys.push_back(k.string());
    kit.burstBands = rows<2>(v.at("burst_bands"));
    kit.burstNormCorr = nums(v.at("burst_norm_corr"));
    if (kit.burstNormCorr.size() != kit.burstBands.size()) throw std::runtime_error("bake: burst_norm_corr size");
}

KitData loadKitFromDirectory(const std::string& dir)
{
    KitData kit;
    std::vector<std::string> keys;
    parseKitHeader(readFile(dir + "/kit.bake.json"), kit, keys);
    for (const auto& k : keys) kit.drums.push_back(parseDrum(readFile(dir + "/" + k + ".bake.json")));
    return kit;
}

} // namespace tympanis
