// Engine.h — the Tympanis modal drum engine (C++ port of prototype/engine.py, md5 735fd0ea).
//
// One resonator bank per drum: re-strikes add into the ringing mode states, so each drum is
// polyphonic for free. Everything is physical modelling from the baked modal data; there is no
// recorded audio anywhere. No JUCE, no allocation after prepare().
//
// Port notes (deliberate differences from the Python prototype):
//   - Noise is a different random sequence (the levels and spectra are the same).
//   - The hit-to-hit variation uses its own random generator, seeded per drum.
//   - Strike-position gain (head and rim) and mode shapes come from baked radius grids (step 0.01) and are
//     linearly interpolated; at the grid points they are exact.
//   - A slot that has decayed 140 dB below a full hit is cleared and skipped (CPU); glide below
//     1e-12 is treated as ended, so the poles stop being recomputed.
//   - With lug_detune turned to 0 while twins still ring, the twins keep ringing at the even
//     tension pitch (the prototype froze them and resumed them later).
//   - Transients, pulses and bursts live in fixed pools; the oldest is replaced when full.
#pragma once

#include "Filters.h"
#include "KitData.h"

#include <complex>
#include <cstdint>
#include <vector>

namespace tympanis
{

struct DrumParams
{
    double tuneSemi = 0.0;      // -2 .. 2
    double lugDetune = 0.0;     // 0 .. 1
    double decay = 1.0;         // 0.25 .. 4
    double glideAmt = 1.0;      // 0 .. 3
    double clickMix = 1.0;      // 0 .. 2
    double noiseMix = 1.0;      // 0 .. 2
    double velRangeDb = 30.0;   // 0 .. 48
    double strikePos = -1.0;    // < 0: native (0.35); not on kicks
    double sizeIn = -1.0;       // < 0: catalog size
    double tubeLen = 1.0;       // octobans only
};

class Drum
{
public:
    void init(const DrumData* data, const KitData* kit, bool restrikeDamp = true, bool variation = true, int maxBlock = 1024);
    void prepare(double sampleRate);
    void reset();

    void strike(double vel, Art art);
    // Adds n samples to out.
    void render(double* out, int n);

    void setTune(double semi);
    void setLugDetune(double x);
    void setDecay(double x);
    void setGlideAmt(double x) { glideAmt_ = x; }
    void setClickMix(double x) { clickMix_ = x; }
    void setNoiseMix(double x) { noiseMix_ = x; }
    void setVelRangeDb(double x) { velRange_ = x; }
    // A fixed level trim for one articulation (gain, 1 = none): scales everything its strikes
    // make (modes, noise, contact transient).
    void setArtTrim(Art a, double gain) { artTrim_[(size_t) a] = gain; }
    void setStrikePos(double r);
    void setSize(double inches);
    void setTubeLen(double x);
    // Shell depth (plugin only, not in the prototype; Kris, 2026-11-25), inches: the recorded
    // shell's and the one played. Equal: nothing changes. See depthPitch() and depthDecay().
    void setDepth(double recordedIn, double depthIn);

    double tuneRatio() const { return tune_; }
    double decayMul(double f) const;            // multiplies decay rates (decay parameter)
    const DrumData& data() const { return *d_; }
    bool silent() const { return ! bankActive_ && ! twinActive_ && nTr_ == 0 && nPulse_ == 0 && nBurst_ == 0 && ! noiseActive_; }
    double bandNorm(int j) const { return bnorm_[(size_t) j]; }

    // Test hook: replace all noise by silence (deterministic output for the reference test).
    void setNoiseless(bool b) { noiseless_ = b; }

private:
    using C = std::complex<double>;
    struct Ramp { int start, len; int pos; };        // re-strike damping ramp on a transient
    struct Transient
    {
        int nm = 0, pos = 0, nRamps = 0;
        double tuneUsed = 0;
        // per mode: f, d, state, step
        double f[64], d[64];
        C c[64], p[64];
        Ramp ramps[4];
        double fac[4][64];
    };
    struct Pulse { double tc, fp, a; int pos; };
    struct Burst { double pk[32], tp[32], dd[32], e[32]; bool decaying[32]; int pos; };

    void useArt(Art a);
    void contactDamp(double vel, Art art, std::vector<double>& fac, double& tc);
    void strikeGain(double r, Art art, std::vector<double>& g) const;
    void gridRow(const std::vector<std::vector<double>>& grid, double r, std::vector<double>& out) const;
    void updateDecayCache();
    void designBands();
    Transient& newTransient();
    Pulse& newPulse();
    Burst& newBurst();
    void computePoles(bool all, double gam);
    void renderBank(double* out, int n);
    void renderNoise(double* out, int n);
    void renderTransients(double* out, int n);
    void renderPulses(double* out, int n);
    void renderBursts(double* out, int n);
    void renderBurstsPart(double* out, int n);

    const DrumData* d_ = nullptr;
    const KitData* kit_ = nullptr;
    double sr_ = 44100;
    bool restrike_ = true, variation_ = true, noiseless_ = false;
    bool isKick_ = false, isOctoban_ = false;
    int K_ = 0;
    Art art_ = Art::Head;
    const std::vector<Layer>* L_ = nullptr;

    // parameters
    double tune_ = 1, lug_ = 0, decay_ = 1, glideAmt_ = 1, clickMix_ = 1, noiseMix_ = 1, velRange_ = 30;
    std::array<double, kNumArts> artTrim_ { 1.0, 1.0, 1.0 };
    double strikePos_ = 0.35, sf_ = 1, sizeIn_ = 14, tubeLen_ = 1, depthQ_ = 1, recDepthIn_ = 10;
    double depthPitch(double f) const;
    double depthDecay(double f) const;
    std::vector<double> tubefac_;

    // bank state
    std::vector<C> z_, zrm_;
    std::vector<double> F_, D_, gw_, dmul_;
    bool hasGw_ = false, bankActive_ = false, zrmValid_ = false;
    int rmLeft_ = 0, rmTot_ = 1;
    double gam_ = 0, gam2_ = 0, tau_ = 0.05, tau2_ = 1.0;
    // lug twins
    std::vector<C> zt_, zrmt_;
    bool twinActive_ = false, zrmtValid_ = false;
    // scratch
    std::vector<C> pblk_, ptw_;
    std::vector<double> wblk_, mag_, gr_, gi_, hr_, hi_;
    std::vector<int> act_;
    int nAct_ = 0;
    bool polesDirty_ = true, magDirty_ = true;
    std::vector<double> scratchA_, scratchB_, scratchC_;

    // noise bands
    std::vector<BandPass> bp_;
    NoiseBank noise_;
    std::vector<double> bnorm_, env_, nd_, bandfc_, bmul_, effEnv_, effR_;
    bool noiseActive_ = false;
    Noise vrng_ { 1 };

    // modelled contact transient
    std::vector<Transient> tr_;
    int nTr_ = 0;
    std::vector<Pulse> pulses_;
    int nPulse_ = 0;
    std::vector<Burst> bursts_;
    int nBurst_ = 0;
    std::vector<BandPass> bbp_;
    NoiseBank burstNoise_;
    std::vector<double> bbnorm_;
    std::vector<double> benv_;            // scratch: summed burst envelopes [sample][band]
    int maxBlock_ = 1024;
};

class Kit
{
public:
    static constexpr int kChunk = 1024;   // default internal render chunk (a multiple of the 32-sample bank block)

    // chunk: the longest piece rendered in one go (a multiple of 32). The prototype renders each
    // stretch between hits in one call; tympanis_render --one-call does the same with a long chunk.
    void init(const KitData* data, bool restrikeDamp = true, bool variation = true, int chunk = kChunk);
    void prepare(double sampleRate);
    void reset();

    int numDrums() const { return (int) drums_.size(); }
    Drum& drum(int i) { return drums_[(size_t) i]; }
    void strike(int drum, double vel, Art art) { drums_[(size_t) drum].strike(vel, art); }
    void setSnareBuzz(double x) { buzz_ = x < 0 ? 0 : x > 1 ? 1 : x; }
    void setNoiseless(bool b);

    // Writes n samples (mono kit mix, every drum at unity) to out.
    void render(double* out, int n);

    // Per-drum output gains for renderStereo (level, pan and any master gain folded in). Changes
    // ramp linearly over the next render call. The drums' acoustic coupling (snare buzz) uses
    // the unfaded sound; a snare's sympathetic head and wire buzz follow that snare's gains.
    void setDrumGains(int drum, double left, double right);
    void snapGains() { gainL_ = targetL_; gainR_ = targetR_; }
    // Writes n samples of the stereo mix to L and R.
    void renderStereo(double* L, double* R, int n);

private:
    using C = std::complex<double>;
    struct Sym
    {
        int drum = 0;
        std::vector<double> F, D, c, w;
        std::vector<C> y, p;
        std::vector<double> yr, yi, pr, pi;
        double href = 1, env = 0;
        NoiseBank wires;                      // gain per band: band norm x wire-noise peak
    };
    void renderChunk(double* out, double* L, double* R, int n);

    const KitData* data_ = nullptr;
    std::vector<Drum> drums_;
    std::vector<Sym> sym_;
    double sr_ = 44100, buzz_ = 0.5;
    int chunk_ = kChunk;
    bool noiseless_ = false;
    std::vector<std::vector<double>> outs_;
    std::vector<double> mix_, head_, drive_, wire_;
    std::vector<double> gainL_, gainR_, targetL_, targetR_;
};

} // namespace tympanis
