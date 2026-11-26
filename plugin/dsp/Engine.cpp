#include "Engine.h"

#include "JobPool.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace tympanis
{

namespace
{
constexpr int kBlock = 32;             // bank coefficient update block (as the prototype)
constexpr int kMaxTransients = 24;
constexpr int kMaxPulses = 256;
constexpr int kMaxBursts = 8;
constexpr int kBurstBlock = 1024;     // bursts render in pieces of at most this (scratch size)
// A slot (or the whole bank) whose |z|^2 falls below this is cleared and skipped: -140 dB
// below a full hit's level (engine output peaks near 1).
constexpr double kSilent = 1e-14;

double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }

// r e^{i th} for any sign of r (std::polar needs r >= 0)
std::complex<double> cpol(double r, double th) { return { r * std::cos(th), r * std::sin(th) }; }

uint32_t crc32(const std::string& s)
{
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : s)
    {
        c ^= ch;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
} // namespace

// ------------------------------------------------------------------------------------------------
// Drum

void Drum::init(const DrumData* data, const KitData* kit, bool restrikeDamp, bool variation, int maxBlock)
{
    d_ = data;
    kit_ = kit;
    restrike_ = restrikeDamp;
    variation_ = variation;
    K_ = d_->K;
    isKick_ = d_->kind == DrumKind::Kick;
    isOctoban_ = d_->kind == DrumKind::Octoban;
    strikePos_ = d_->r0;
    sizeIn_ = d_->nativeIn;
    sf_ = 1;
    tubeLen_ = 1;
    tubefac_.assign((size_t) K_, 1.0);
    const size_t K = (size_t) K_, S = d_->lug.slots.size(), nb = d_->bands.size();
    z_.assign(K, 0); zrm_.assign(K, 0);
    F_.assign(K, 0); D_.assign(K, 0); gw_.assign(K, 1); dmul_.assign(K, 1);
    zt_.assign(S, 0); zrmt_.assign(S, 0);
    pblk_.assign(K, 0); ptw_.assign(S, 0); wblk_.assign(K, 0); mag_.assign(K, 0); act_.assign(K, 0);
    gr_.assign(std::max({ K, S, (size_t) 64 }), 0); gi_ = gr_; hr_ = gr_; hi_ = gr_;
    scratchA_.assign(K, 0); scratchB_.assign(K, 0); scratchC_.assign(K, 0);
    bp_.assign(nb, BandPass {});
    bnorm_.assign(nb, 0); env_.assign(nb, 0); nd_.assign(nb, 0); bandfc_.assign(nb, 0); bmul_.assign(nb, 1);
    effEnv_.assign(nb, 0); effR_.assign(nb, 0);
    noise_.resize((int) nb, 7 + crc32(d_->model));
    for (size_t j = 0; j < nb; ++j) bandfc_[j] = std::sqrt(d_->bands[j][0] * d_->bands[j][1]);
    tr_.assign(kMaxTransients, Transient {});
    pulses_.assign(kMaxPulses, Pulse {});
    bursts_.assign(kMaxBursts, Burst {});
    const size_t nbb = kit_->burstBands.size();
    if (nbb > 32) throw std::runtime_error("too many burst bands");
    for (int a = 0; a < kNumArts; ++a)
        for (const auto& l : d_->arts[(size_t) a])
        {
            if (l.trModes.size() > 64) throw std::runtime_error("transient has more than 64 modes");
            if (! l.trBurst.empty() && l.trBurst.size() != nbb) throw std::runtime_error("burst band count differs");
        }
    bbp_.assign(nbb, BandPass {});
    bbnorm_.assign(nbb, 0);
    burstNoise_.resize((int) nbb, 11 + crc32(d_->model));
    maxBlock_ = maxBlock;
    benv_.assign(nbb * (size_t) kBurstBlock, 0);
    vrng_.reseed(crc32(d_->model + ".json"));
    useArt(Art::Head);
    // the prototype starts from the hardest head layer's frequencies
    F_ = (*L_)[0].freq;
    D_ = (*L_)[0].decay;
    nd_ = (*L_).back().noiseDecay;
    updateDecayCache();
}

void Drum::prepare(double sampleRate)
{
    sr_ = sampleRate;
    designBands();
    for (size_t j = 0; j < bbp_.size(); ++j)
    {
        const double lo = kit_->burstBands[j][0];
        const double hi = std::min({ kit_->burstBands[j][1], 20000.0, 0.45 * sr_ });
        if (lo >= 0.95 * hi) { bbnorm_[j] = 0; burstNoise_.setBand((int) j, BandPass {}, 0); continue; }
        bbp_[j].design(lo, hi, sr_);
        bbnorm_[j] = kit_->burstNormCorr[j] * bbp_[j].whiteNoiseNorm(sr_);
        burstNoise_.setBand((int) j, bbp_[j], bbnorm_[j]);
    }
    reset();
}

void Drum::designBands()
{
    for (size_t j = 0; j < bp_.size(); ++j)
    {
        const double lo0 = d_->bands[j][0] * sf_, hi0 = std::min(d_->bands[j][1] * sf_, 21000.0);
        const double lo = std::max(lo0, 20.0);
        const double hi = std::min(std::max(hi0, lo0 * 1.05 + 1), 0.45 * sr_);
        if (lo >= 0.95 * hi) { bnorm_[j] = 0; bp_[j] = BandPass {}; noise_.setBand((int) j, bp_[j], 0); continue; }
        bp_[j].design(lo, hi, sr_);
        bnorm_[j] = d_->bandNormCorr[j] * bp_[j].whiteNoiseNorm(sr_);
        noise_.setBand((int) j, bp_[j], bnorm_[j]);
    }
}

void Drum::reset()
{
    std::fill(z_.begin(), z_.end(), C(0));
    std::fill(zrm_.begin(), zrm_.end(), C(0));
    std::fill(zt_.begin(), zt_.end(), C(0));
    std::fill(zrmt_.begin(), zrmt_.end(), C(0));
    bankActive_ = twinActive_ = zrmValid_ = zrmtValid_ = false;
    rmLeft_ = 0; rmTot_ = 1;
    gam_ = gam2_ = 0;
    magDirty_ = polesDirty_ = true;
    std::fill(env_.begin(), env_.end(), 0.0);
    noiseActive_ = false;
    noise_.reset();
    burstNoise_.reset();
    nTr_ = nPulse_ = nBurst_ = 0;
}

void Drum::useArt(Art a)
{
    if (! d_->hasArt(a)) a = Art::Head;
    art_ = a;
    L_ = &d_->arts[(size_t) a];
    hasGw_ = ! (*L_)[0].glideW.empty();
}

double Drum::decayMul(double f) const
{
    if (decay_ == 1.0) return 1.0;
    const double w = clampd(1 - 0.35 * std::log2(std::max(f, 1.0) / d_->fref), 0.3, 1.0);
    return std::pow(decay_, -w);
}

void Drum::updateDecayCache()
{
    for (int k = 0; k < K_; ++k) dmul_[(size_t) k] = decayMul(F_[(size_t) k]);
    for (size_t j = 0; j < bmul_.size(); ++j) bmul_[j] = decayMul(bandfc_[j]);
}

void Drum::setTune(double semi)
{
    const double t = std::pow(2.0, clampd(semi, -2, 2) / 12.0);
    if (t != tune_) { tune_ = t; polesDirty_ = true; }
}
void Drum::setLugDetune(double x)
{
    // 0 .. 2: 1 is the prototype's lug_detune 1 (5 % RMS); the plugin's range reaches 2 (10 %)
    const double l = clampd(x, 0, 2);
    if (l != lug_) { lug_ = l; polesDirty_ = true; }
}
void Drum::setDecay(double x)
{
    decay_ = clampd(x, 0.25, 4.0);
    updateDecayCache();
    magDirty_ = polesDirty_ = true;
}
void Drum::setStrikePos(double r)
{
    if (isKick_) return;
    strikePos_ = r < 0 ? d_->r0 : clampd(r, 0, 0.95);
}
void Drum::setSize(double inches)
{
    const double D0 = d_->nativeIn;
    sizeIn_ = inches < 0 ? D0 : clampd(inches, 0.5 * D0, 2.0 * D0);
    sf_ = D0 / sizeIn_;
    magDirty_ = polesDirty_ = true;
    designBands();      // coefficients only; call from the audio thread only between blocks
}
// Shell depth. The models hold one depth per drum (as recorded); a deeper or shallower shell is
// modelled from the air it encloses, q = recorded depth / depth:
//  - the air between the heads stiffens the modes that move it (the fundamental's group, below
//    1.3 x the main mode): f^2 = f_head^2 + f_air^2 with f_air^2 proportional to 1/depth, the air
//    taking kAirShare of the recorded stiffness, so f' = f sqrt((1 + kAirShare q) / (1 + kAirShare));
//    the same group sustains as sqrt(depth) (shallower: shorter, deeper: longer);
//  - the shell's first axial air mode (c / 2 depth) moves as 1 / depth: the slots within 3 % of it.
// Only the plugin sets it (Toms 1-4, the Size parameter); q = 1 is bit-identical to before.
namespace
{
constexpr double kAirShare = 0.12, kLowGroup = 1.3, kSoundInPerS = 13504.0, kCavityWindow = 0.03;
}
void Drum::setDepth(double recordedIn, double depthIn)
{
    if (recordedIn <= 0 || depthIn <= 0) return;
    recDepthIn_ = recordedIn;
    const double q = clampd(recordedIn / depthIn, 0.25, 4.0);
    if (q != depthQ_) { depthQ_ = q; magDirty_ = polesDirty_ = true; }
}
double Drum::depthPitch(double f) const
{
    if (depthQ_ == 1) return 1;
    if (f < kLowGroup * d_->fMain) return std::sqrt((1 + kAirShare * depthQ_) / (1 + kAirShare));
    const double fc = kSoundInPerS / (2 * recDepthIn_);  // the recorded shell's first axial mode
    if (std::fabs(f / fc - 1) < kCavityWindow) return depthQ_;
    return 1;
}
double Drum::depthDecay(double f) const
{
    if (depthQ_ == 1 || f >= kLowGroup * d_->fMain) return 1;
    return std::sqrt(depthQ_);   // decay rate: shallower (q > 1) faster
}

void Drum::setTubeLen(double x)
{
    tubeLen_ = clampd(x, 0.5, 2.0);
    if (isOctoban_ && d_->tubeCluster.size() == (size_t) K_)
        for (int k = 0; k < K_; ++k) tubefac_[(size_t) k] = d_->tubeCluster[(size_t) k] ? 1.0 / tubeLen_ : 1.0;
    polesDirty_ = true;
}

void Drum::gridRow(const std::vector<std::vector<double>>& grid, double r, std::vector<double>& out) const
{
    const int n = (int) grid.size();
    const double inv = std::round(1.0 / d_->gridStep);
    double x = clampd(r * inv, 0.0, (double) (n - 1));
    const double xr = std::round(x);
    if (std::fabs(x - xr) < 1e-9)
    {
        out = grid[(size_t) xr];
        return;
    }
    int i = (int) std::floor(x);
    if (i >= n - 1) i = n - 2;
    const double fr = x - i;
    const auto& a = grid[(size_t) i];
    const auto& b = grid[(size_t) i + 1];
    for (int k = 0; k < K_; ++k) out[(size_t) k] = (1 - fr) * a[(size_t) k] + fr * b[(size_t) k];
}

void Drum::strikeGain(double r, Art art, std::vector<double>& g) const
{
    const auto& grid = d_->strikeGainGrid[(size_t) art];
    if (isKick_ || r == d_->r0 || grid.empty())
    {
        std::fill(g.begin(), g.end(), 1.0);
        return;
    }
    gridRow(grid, r, g);
}

void Drum::contactDamp(double vel, Art art, std::vector<double>& fac, double& tc)
{
    double r;
    if (isKick_) { r = 0.05; tc = 4.5e-3 * std::pow(vel + 0.1, -0.2); }
    else if (art == Art::Rim) { r = 0.9; tc = 1.5e-3 * std::pow(vel + 0.1, -0.2); }
    else if (art == Art::Side) { r = 0.55; tc = 5e-3; }
    else { r = strikePos_; tc = 2.0e-3 * std::pow(vel + 0.1, -0.2); }
    gridRow(d_->shape2Grid, r, fac);          // fac holds s2 here
    const double v = 0.4 + 0.6 * vel;
    for (int k = 0; k < K_; ++k)
    {
        const double s2 = clampd(fac[(size_t) k], 0, 1);
        fac[(size_t) k] = std::exp(-v * s2 * (0.2 + 0.74 * std::min(F_[(size_t) k] * tune_ * tc, 3.0)));
    }
}

Drum::Transient& Drum::newTransient()
{
    if (nTr_ < kMaxTransients) return tr_[(size_t) nTr_++];
    // full: replace the oldest
    int oldest = 0;
    for (int i = 1; i < nTr_; ++i) if (tr_[(size_t) i].pos > tr_[(size_t) oldest].pos) oldest = i;
    return tr_[(size_t) oldest];
}
Drum::Pulse& Drum::newPulse()
{
    if (nPulse_ < kMaxPulses) return pulses_[(size_t) nPulse_++];
    int oldest = 0;
    for (int i = 1; i < nPulse_; ++i) if (pulses_[(size_t) i].pos > pulses_[(size_t) oldest].pos) oldest = i;
    return pulses_[(size_t) oldest];
}
Drum::Burst& Drum::newBurst()
{
    if (nBurst_ < kMaxBursts) return bursts_[(size_t) nBurst_++];
    int oldest = 0;
    for (int i = 1; i < nBurst_; ++i) if (bursts_[(size_t) i].pos > bursts_[(size_t) oldest].pos) oldest = i;
    return bursts_[(size_t) oldest];
}

void Drum::strike(double vel, Art art)
{
    useArt(art);
    art = art_;
    const std::vector<Layer>& L = *L_;
    const size_t K = (size_t) K_, S = d_->lug.slots.size();
    vel = clampd(vel, 0.0, 1.0);
    const bool twinsOn = (lug_ > 0 || twinActive_) && S > 0;

    // ---- re-strike damping: the stick loads what is already ringing
    if (restrike_ && bankActive_)
    {
        std::vector<double>& fac = scratchA_;
        double tc = 0;
        contactDamp(vel, art, fac, tc);
        if (! (zrmValid_ && rmLeft_ > 0)) std::fill(zrm_.begin(), zrm_.end(), C(0));
        for (size_t k = 0; k < K; ++k) zrm_[k] += z_[k] * (1 - fac[k]);
        zrmValid_ = true;
        if (twinsOn)
        {
            if (! (zrmtValid_ && rmLeft_ > 0)) std::fill(zrmt_.begin(), zrmt_.end(), C(0));
            for (size_t s = 0; s < S; ++s) zrmt_[s] += zt_[s] * (1 - fac[(size_t) d_->lug.slots[s]]);
            zrmtValid_ = true;
        }
        rmTot_ = std::max(1, (int) (tc * sr_));
        rmLeft_ = rmTot_;
        const double em = 0.5 + 0.5 * (1 - 0.5 * vel);        // wires re-seat under the new hit
        for (auto& e : env_) e *= em;
        // the previous hits' short contact modes are loaded by the stick too
        const double v = 0.4 + 0.6 * vel;
        for (int t = 0; t < nTr_; ++t)
        {
            Transient& tr = tr_[(size_t) t];
            if (tr.nRamps == 4)
            {
                // fold the oldest ramp in early (rare: four re-strikes within one contact time)
                for (int q = 0; q < tr.nm; ++q) tr.c[q] *= tr.fac[0][q];
                for (int r = 1; r < 4; ++r) { tr.ramps[r - 1] = tr.ramps[r]; std::memcpy(tr.fac[r - 1], tr.fac[r], sizeof(tr.fac[r])); }
                tr.nRamps = 3;
            }
            Ramp& rp = tr.ramps[tr.nRamps];
            rp.start = tr.pos;
            rp.len = std::max(1, (int) (tc * sr_));
            for (int q = 0; q < tr.nm; ++q)
                tr.fac[tr.nRamps][q] = std::exp(-v * 0.3 * (0.2 + 0.74 * std::min(tr.f[q] * tune_ * tc, 3.0)));
            ++tr.nRamps;
        }
    }

    // ---- hit-to-hit variation
    double tilt = 0, pos = strikePos_;
    if (variation_)
    {
        vel = clampd(vel + 0.02 * vrng_.normal(), 0.0, 1.0);
        tilt = 0.4 * vrng_.normal();                     // dB per octave above 2x the main mode
        if (! isKick_ && art == Art::Head) pos = clampd(strikePos_ + 0.015 * vrng_.normal(), 0.0, 0.95);
    }

    // ---- layer interpolation
    const int nl = (int) L.size();
    const double lp = (1 - vel) * (nl - 1);
    const int i = (int) std::floor(lp), i2 = std::min(i + 1, nl - 1);
    const double fr = lp - i;
    const Layer& A = L[(size_t) i];
    const Layer& B = L[(size_t) i2];
    const double gain = artTrim_[(size_t) art] * std::pow(10.0, -velRange_ * std::pow(1 - vel, 1.3) / 20.0);

    std::vector<double>& sg = scratchB_;
    if (art != Art::Side) strikeGain(pos, art, sg);
    else std::fill(sg.begin(), sg.end(), 1.0);
    std::vector<double>& amp = scratchC_;
    const std::vector<double>& ph = fr < 0.5 ? A.phase : B.phase;
    for (size_t k = 0; k < K; ++k)
    {
        double a = std::exp((1 - fr) * std::log(A.amp[k] + 1e-9) + fr * std::log(B.amp[k] + 1e-9));
        if (tilt != 0)
            a *= std::pow(10.0, tilt * std::log2(std::max(A.freq[k] / (2 * d_->fMain), 1.0)) / 20.0);
        amp[k] = gain * a * sg[k];
    }
    // new hit's mode amplitudes; with uneven lug tension the split pairs share them by strike angle
    if (lug_ > 0 && S > 0)
    {
        const double th = d_->lug.theta + (variation_ ? 0.15 * vrng_.normal() : 0.0);
        for (size_t s = 0; s < S; ++s)
        {
            const size_t k = (size_t) d_->lug.slots[s];
            const double dl = d_->lug.m[s] * (th - d_->lug.phi[s]);
            const C b = cpol(amp[k], ph[k]);
            zt_[s] += b * std::sin(dl);
            amp[k] *= std::cos(dl);
        }
        twinActive_ = true;
    }
    for (size_t k = 0; k < K; ++k)
    {
        z_[k] += cpol(amp[k], ph[k]);
        F_[k] = (1 - fr) * A.freq[k] + fr * B.freq[k];
        D_[k] = (1 - fr) * A.decay[k] + fr * B.decay[k];
        gw_[k] = hasGw_ ? (1 - fr) * A.glideW[k] + fr * B.glideW[k] : 1.0;
    }
    bankActive_ = true;
    updateDecayCache();
    magDirty_ = polesDirty_ = true;

    // ---- tension-modulation glide (accumulates in rolls, capped)
    const double gt = ((1 - fr) * A.glideG + fr * B.glideG) * glideAmt_;
    tau_ = (1 - fr) * A.glideTau + fr * B.glideTau;
    if (gt > 0) gam_ = std::min(gam_ + gt, d_->glideGmax * glideAmt_);
    const double gt2 = ((1 - fr) * A.glideG2 + fr * B.glideG2) * glideAmt_;
    tau2_ = (1 - fr) * A.glideTau2 + fr * B.glideTau2;
    if (gt2 > 0) gam2_ = std::min(gam2_ + gt2, 1.3 * std::max(L[0].glideG2, 1e-6) * glideAmt_);

    // ---- noise layer
    for (size_t j = 0; j < env_.size(); ++j)
    {
        env_[j] += gain * std::exp((1 - fr) * std::log(A.noisePeak[j] + 1e-12) + fr * std::log(B.noisePeak[j] + 1e-12)) * noiseMix_;
        nd_[j] = (1 - fr) * A.noiseDecay[j] + fr * B.noiseDecay[j];
    }
    noiseActive_ = true;

    // ---- modelled contact transient: damped modes + contact pulses (both layers, crossfaded) + one noise burst
    if (clickMix_ > 0)
    {
        const std::pair<double, const Layer*> ws[2] = { { 1 - fr, &A }, { fr, &B } };
        for (const auto& [w, ly] : ws)
        {
            if (w < 1e-6) continue;
            const double g = gain * clickMix_ * w;
            if (! ly->trModes.empty())
            {
                Transient& tr = newTransient();
                tr.nm = (int) ly->trModes.size();
                tr.pos = 0;
                tr.nRamps = 0;
                tr.tuneUsed = 0;              // forces the step update
                for (int q = 0; q < tr.nm; ++q)
                {
                    const auto& m = ly->trModes[(size_t) q];
                    tr.f[q] = m[0];
                    tr.d[q] = m[1];
                    tr.c[q] = cpol(g * m[2], m[3]);
                }
            }
            for (const auto& p : ly->trPulses)
            {
                Pulse& pu = newPulse();
                pu.tc = p[0]; pu.fp = p[1]; pu.a = g * p[2]; pu.pos = 0;
            }
        }
        if (! A.trBurst.empty() && ! B.trBurst.empty())
        {
            Burst& bu = newBurst();
            bu.pos = 0;
            for (size_t j = 0; j < A.trBurst.size(); ++j)
            {
                bu.pk[j] = gain * clickMix_ * std::exp((1 - fr) * std::log(A.trBurst[j][0] + 1e-12) + fr * std::log(B.trBurst[j][0] + 1e-12));
                bu.tp[j] = (1 - fr) * A.trBurst[j][1] + fr * B.trBurst[j][1];
                bu.dd[j] = (1 - fr) * A.trBurst[j][2] + fr * B.trBurst[j][2];
                bu.decaying[j] = false;
                bu.e[j] = 0;
            }
        }
    }
}

void Drum::render(double* out, int n)
{
    if (n <= 0) return;
    renderBank(out, n);
    renderNoise(out, n);
    renderTransients(out, n);
    renderPulses(out, n);
    renderBursts(out, n);
}

void Drum::computePoles(bool all, double gam)
{
    // per-slot pole: decay and pitch (tune, size, tube, glide); lug split moves the main slot up, the twin down
    const double twoPiOverSr = 2 * kPi / sr_;
    if (magDirty_)
    {
        for (int k = 0; k < K_; ++k) mag_[(size_t) k] = std::exp(-D_[(size_t) k] * dmul_[(size_t) k] * sf_ * depthDecay(F_[(size_t) k]) / sr_);
        magDirty_ = false;
    }
    auto pole = [&](size_t k) {
        const double w = twoPiOverSr * F_[k] * depthPitch(F_[k]) * tune_ * sf_ * tubefac_[k] * (1 + gam * gw_[k]);
        wblk_[k] = w;
        pblk_[k] = std::polar(mag_[k], w);
    };
    if (all) for (size_t k = 0; k < (size_t) K_; ++k) pole(k);
    else for (int j = 0; j < nAct_; ++j) pole((size_t) act_[(size_t) j]);
    const size_t S = d_->lug.slots.size();
    if ((lug_ > 0 || twinActive_) && S > 0)
    {
        const double scale = lug_ * d_->lug.sigma;
        for (size_t s = 0; s < S; ++s)
        {
            const size_t k = (size_t) d_->lug.slots[s];
            const double dl = scale * d_->lug.rel[s];
            const double w = twoPiOverSr * F_[k] * depthPitch(F_[k]) * tune_ * sf_ * tubefac_[k] * (1 + gam * gw_[k]);
            ptw_[s] = std::polar(mag_[k], w * (1 - dl));      // the lower eigenmode
            pblk_[k] = std::polar(mag_[k], w * (1 + dl));     // main slot: the upper eigenmode
        }
    }
}

// Rings a set of complex states through one block (SIMD across slots), adding the real parts to out.
static double ringBlock(std::complex<double>* z, const std::complex<double>* p, const int* idx, int n,
                        double* ar, double* ai, double* cr, double* ci, double* out, int b)
{
    for (int j = 0; j < n; ++j)
    {
        const int k = idx ? idx[j] : j;
        ar[j] = z[k].real(); ai[j] = z[k].imag(); cr[j] = p[k].real(); ci[j] = p[k].imag();
    }
    for (int i = 0; i < b; ++i)
    {
        double acc = 0;
#pragma omp simd reduction(+ : acc)
        for (int j = 0; j < n; ++j)
        {
            const double re = ar[j] * cr[j] - ai[j] * ci[j];
            const double im = ar[j] * ci[j] + ai[j] * cr[j];
            ar[j] = re; ai[j] = im;
            acc += re;
        }
        out[i] += acc;
    }
    double e = 0;
    for (int j = 0; j < n; ++j)
    {
        const int k = idx ? idx[j] : j;
        z[k] = { ar[j], ai[j] };
        e += ar[j] * ar[j] + ai[j] * ai[j];
    }
    return e;
}

void Drum::renderBank(double* out, int n)
{
    const size_t K = (size_t) K_, S = d_->lug.slots.size();
    if (! bankActive_ && ! twinActive_)
    {
        // nothing ringing: only the glide state moves on
        gam_ *= std::exp(-n / (tau_ * sr_));
        if (gam2_ != 0) gam2_ *= std::exp(-n / (tau2_ * sr_));
        rmLeft_ = 0;
        return;
    }
    for (int k0 = 0; k0 < n; k0 += kBlock)
    {
        const int b = std::min(kBlock, n - k0);
        double gam = gam2_ != 0 ? gam_ + gam2_ : gam_;
        gam_ *= std::exp(-b / (tau_ * sr_));
        if (gam2_ != 0) gam2_ *= std::exp(-b / (tau2_ * sr_));
        if (gam < 1e-12)
        {
            // glide has died away: the poles stop moving and are reused until something changes
            if (gam != 0 || gam_ != 0 || gam2_ != 0) polesDirty_ = true;
            gam = gam_ = gam2_ = 0;
        }

        // re-strike damping: remove the loaded part of what was ringing, over the contact time
        if (rmLeft_ > 0)
        {
            if (polesDirty_ || gam != 0) { computePoles(true, gam); polesDirty_ = false; }
            const double f = (double) std::min(b, rmLeft_) / rmTot_;
            if (twinActive_ && zrmtValid_)
                for (size_t s = 0; s < S; ++s)
                {
                    zt_[s] -= zrmt_[s] * f;
                    zrmt_[s] *= std::pow(ptw_[s], b);
                }
            for (size_t k = 0; k < K; ++k)
            {
                z_[k] -= zrm_[k] * f;
                zrm_[k] *= std::pow(pblk_[k], b);
            }
            rmLeft_ -= b;
        }
        // slots still ringing
        nAct_ = 0;
        for (size_t k = 0; k < K; ++k)
            if (std::norm(z_[k]) > kSilent) act_[(size_t) nAct_++] = (int) k;
        if (polesDirty_ || gam != 0)
        {
            computePoles(polesDirty_, gam);
            polesDirty_ = false;
        }
        if (twinActive_ && S > 0)
        {
            const double e = ringBlock(zt_.data(), ptw_.data(), nullptr, (int) S, gr_.data(), gi_.data(), hr_.data(), hi_.data(), out + k0, b);
            if (e < kSilent * (double) S && rmLeft_ <= 0)
            {
                std::fill(zt_.begin(), zt_.end(), C(0));
                twinActive_ = false;
                zrmtValid_ = false;
            }
        }
        ringBlock(z_.data(), pblk_.data(), act_.data(), nAct_, gr_.data(), gi_.data(), hr_.data(), hi_.data(), out + k0, b);
        if (nAct_ == 0 && rmLeft_ <= 0)
        {
            std::fill(z_.begin(), z_.end(), C(0));
            bankActive_ = false;
            zrmValid_ = false;
            if (! twinActive_)
            {
                const int rest = n - k0 - b;
                if (rest > 0)
                {
                    gam_ *= std::exp(-rest / (tau_ * sr_));
                    if (gam2_ != 0) gam2_ *= std::exp(-rest / (tau2_ * sr_));
                }
                return;
            }
        }
    }
}

void Drum::renderNoise(double* out, int n)
{
    // bands below 1e-7 are off (as the prototype); the rest run side by side
    bool any = false;
    const size_t nb = env_.size();
    for (size_t j = 0; j < nb; ++j)
    {
        const bool on = env_[j] >= 1e-7;
        effEnv_[j] = on ? env_[j] : 0.0;
        effR_[j] = on ? std::exp(-nd_[j] * bmul_[j] / sr_) : 1.0;
        any = any || on;
    }
    noiseActive_ = any;
    if (! any) return;
    if (noiseless_)
        for (size_t j = 0; j < nb; ++j) effEnv_[j] *= std::pow(effR_[j], n);
    else
        noise_.processDecay(out, n, effEnv_.data(), effR_.data());
    for (size_t j = 0; j < nb; ++j)
        if (env_[j] >= 1e-7) env_[j] = effEnv_[j];
}

void Drum::renderTransients(double* out, int n)
{
    const double lifetime = 0.25 * sr_;
    for (int t = 0; t < nTr_; ++t)
    {
        Transient& tr = tr_[(size_t) t];
        if (tr.tuneUsed != tune_)
        {
            for (int q = 0; q < tr.nm; ++q) tr.p[q] = std::polar(std::exp(-tr.d[q] / sr_), 2 * kPi * tr.f[q] * tune_ / sr_);
            tr.tuneUsed = tune_;
        }
        if (tr.nRamps == 0)
        {
            // output then step (the transient starts at t = 0), SIMD across modes
            double* ar = gr_.data(); double* ai = gi_.data(); double* cr = hr_.data(); double* ci = hi_.data();
            const int nm = tr.nm;
            for (int q = 0; q < nm; ++q) { ar[q] = tr.c[q].real(); ai[q] = tr.c[q].imag(); cr[q] = tr.p[q].real(); ci[q] = tr.p[q].imag(); }
            for (int i = 0; i < n; ++i)
            {
                double acc = 0;
#pragma omp simd reduction(+ : acc)
                for (int q = 0; q < nm; ++q)
                {
                    acc += ar[q];
                    const double re = ar[q] * cr[q] - ai[q] * ci[q];
                    const double im = ar[q] * ci[q] + ai[q] * cr[q];
                    ar[q] = re; ai[q] = im;
                }
                out[i] += acc;
            }
            for (int q = 0; q < nm; ++q) tr.c[q] = { ar[q], ai[q] };
        }
        else
        {
            for (int q = 0; q < tr.nm; ++q)
            {
                C c = tr.c[q];
                const C p = tr.p[q];
                for (int i = 0; i < n; ++i)
                {
                    const int idx = tr.pos + i;
                    double m = 1;
                    for (int r = 0; r < tr.nRamps; ++r)
                    {
                        const double rmp = std::min((double) (idx - tr.ramps[r].start) / tr.ramps[r].len, 1.0);
                        m *= 1 - rmp * (1 - tr.fac[r][q]);
                    }
                    out[i] += c.real() * m;
                    c *= p;
                }
                tr.c[q] = c;
            }
            // ramps that have reached 1 for every later sample fold into the state
            int keep = 0;
            for (int r = 0; r < tr.nRamps; ++r)
            {
                if (tr.pos + n - 1 - tr.ramps[r].start >= tr.ramps[r].len)
                {
                    for (int q = 0; q < tr.nm; ++q) tr.c[q] *= tr.fac[r][q];
                }
                else
                {
                    if (keep != r) { tr.ramps[keep] = tr.ramps[r]; std::memcpy(tr.fac[keep], tr.fac[r], sizeof(tr.fac[r])); }
                    ++keep;
                }
            }
            tr.nRamps = keep;
        }
        tr.pos += n;
    }
    int keep = 0;
    for (int t = 0; t < nTr_; ++t)
        if (tr_[(size_t) t].pos < lifetime)
        {
            if (keep != t) tr_[(size_t) keep] = tr_[(size_t) t];
            ++keep;
        }
    nTr_ = keep;
}

void Drum::renderPulses(double* out, int n)
{
    for (int k = 0; k < nPulse_; ++k)
    {
        Pulse& pu = pulses_[(size_t) k];
        const double c = kPi * pu.fp;
        for (int i = 0; i < n; ++i)
        {
            const double t = (pu.pos + i) / sr_ - pu.tc;
            const double u = (c * t) * (c * t);
            if (u > 80) continue;                // below 1e-35
            out[i] += pu.a * (1 - 2 * u) * std::exp(-u);
        }
        pu.pos += n;
    }
    int keep = 0;
    for (int k = 0; k < nPulse_; ++k)
    {
        const Pulse& pu = pulses_[(size_t) k];
        if (pu.pos / sr_ < pu.tc + 3.0 / pu.fp)
        {
            if (keep != k) pulses_[(size_t) keep] = pu;
            ++keep;
        }
    }
    nPulse_ = keep;
}

void Drum::renderBursts(double* out, int n)
{
    for (int k = 0; k < n && nBurst_ > 0; k += kBurstBlock) renderBurstsPart(out + k, std::min(kBurstBlock, n - k));
}

void Drum::renderBurstsPart(double* out, int n)
{
    if (nBurst_ == 0) return;
    const size_t nb = bbp_.size();
    std::fill(benv_.begin(), benv_.begin() + (ptrdiff_t) (nb * (size_t) n), 0.0);   // [sample][band]
    for (size_t j = 0; j < nb; ++j)
    {
        for (int k = 0; k < nBurst_; ++k)
        {
            Burst& bu = bursts_[(size_t) k];
            if (bu.pk[j] <= 0) continue;
            const double tp = bu.tp[j], dd = bu.dd[j], pk = bu.pk[j];
            const double r = std::exp(-dd / sr_), tpd = std::max(tp, 1e-4);
            double e = bu.e[j];
            bool dec = bu.decaying[j];
            for (int i = 0; i < n; ++i)
            {
                const double t = (bu.pos + i) / sr_;
                double v;
                if (t < tp) v = t / tpd;
                else
                {
                    if (! dec) { e = std::exp(-dd * std::max(t - tp, 0.0)); dec = true; }
                    v = e;
                    e *= r;
                }
                benv_[(size_t) i * nb + j] += pk * v;
            }
            bu.e[j] = e;
            bu.decaying[j] = dec;
        }
    }
    if (! noiseless_) burstNoise_.processEnv(out, n, benv_.data(), (int) nb);
    const double lifetime = 0.12 * sr_;
    int keep = 0;
    for (int k = 0; k < nBurst_; ++k)
    {
        bursts_[(size_t) k].pos += n;
        if (bursts_[(size_t) k].pos < lifetime)
        {
            if (keep != k) bursts_[(size_t) keep] = bursts_[(size_t) k];
            ++keep;
        }
    }
    nBurst_ = keep;
}

// ------------------------------------------------------------------------------------------------
// Kit

void Kit::init(const KitData* data, bool restrikeDamp, bool variation, int chunk)
{
    data_ = data;
    chunk_ = std::max(32, chunk / 32 * 32);
    drums_.assign(data->drums.size(), Drum {});
    sym_.clear();
    for (size_t i = 0; i < drums_.size(); ++i)
    {
        drums_[i].init(&data->drums[i], data, restrikeDamp, variation, chunk_);
        const DrumData& d = data->drums[i];
        if (! d.hasSym) continue;
        Sym s;
        s.drum = (int) i;
        s.F = d.sym.freq; s.D = d.sym.decay; s.w = d.sym.w; s.href = d.sym.href;
        s.c.assign(s.F.size(), 0);
        s.y.assign(s.F.size(), 0);
        s.p.assign(s.F.size(), 0);
        s.yr.assign(s.F.size(), 0); s.yi = s.yr; s.pr = s.yr; s.pi = s.yr;
        s.wires.resize((int) d.bands.size(), 23 + (uint32_t) i);
        sym_.push_back(std::move(s));
    }
    outs_.assign(drums_.size(), std::vector<double>((size_t) chunk_, 0.0));
    runOrder_.assign(drums_.size(), 0);
    quiet_.assign(drums_.size(), 0);
    mix_.assign((size_t) chunk_, 0); head_.assign((size_t) chunk_, 0); drive_.assign((size_t) chunk_, 0); wire_.assign((size_t) chunk_, 0);
    gainL_.assign(drums_.size(), 1.0); gainR_ = gainL_; targetL_ = gainL_; targetR_ = gainL_;
}

void Kit::prepare(double sampleRate)
{
    sr_ = sampleRate;
    for (auto& d : drums_) d.prepare(sampleRate);
    for (auto& s : sym_)
    {
        const DrumData& d = data_->drums[(size_t) s.drum];
        for (size_t j = 0; j < d.bands.size(); ++j)
        {
            const double lo = d.bands[j][0], hi = std::min({ d.bands[j][1], 20000.0, 0.45 * sr_ });
            BandPass bp;
            if (lo >= 0.95 * hi) { s.wires.setBand((int) j, bp, 0); continue; }
            bp.design(lo, hi, sr_);
            s.wires.setBand((int) j, bp, d.bandNormCorr[j] * bp.whiteNoiseNorm(sr_) * d.sym.noisePeak[j]);
        }
    }
    reset();
}

void Kit::reset()
{
    for (auto& d : drums_) d.reset();
    for (auto& s : sym_)
    {
        std::fill(s.y.begin(), s.y.end(), C(0));
        s.env = 0;
        s.wires.reset();
    }
}

void Kit::setNoiseless(bool b)
{
    noiseless_ = b;
    for (auto& d : drums_) d.setNoiseless(b);
}

void Kit::render(double* out, int n)
{
    for (int k = 0; k < n; k += chunk_) renderChunk(out + k, nullptr, nullptr, std::min(chunk_, n - k));
}

void Kit::setDrumGains(int drum, double left, double right)
{
    targetL_[(size_t) drum] = left;
    targetR_[(size_t) drum] = right;
}

void Kit::renderStereo(double* L, double* R, int n)
{
    for (int k = 0; k < n; k += chunk_) renderChunk(nullptr, L + k, R + k, std::min(chunk_, n - k));
}

void Kit::renderDrumJob(void* kit, int drum)
{
    Kit& k = *static_cast<Kit*>(kit);
    double* o = k.outs_[(size_t) drum].data();
    std::fill(o, o + k.jobN_, 0.0);
    k.drums_[(size_t) drum].render(o, k.jobN_);
}

// The chunk in four stages. 1: every drum renders into its own buffer; each drum has its own state
// and random sequences and reads only its own and the kit's read-only data, so the drums that are
// sounding go through the pool (if any), heaviest first, and the silent ones are rendered here
// after it (they cost little and keep their state as before). 2-4 stay on this thread, in this
// order, as before: 2: the mix, summed in drum order (so the same bits at any thread count);
// 3: the snare buzz, each snare driven by the mix with the earlier snares' sympathetic ring and
// wires in it (serial by nature); 4: the per-drum gain ramps into L and R, in drum order, after
// the snare pass has added into the snares' own buffers.
void Kit::renderChunk(double* out, double* L, double* R, int n)
{
    int nRun = 0, nQuiet = 0;
    for (int d = 0; d < (int) drums_.size(); ++d)
    {
        if (drums_[(size_t) d].silent()) { quiet_[(size_t) nQuiet++] = d; continue; }
        // heaviest first: by ringing slots, most first (ties keep drum order); insertion sort, 17 at most
        const int c = drums_[(size_t) d].ringingSlots();
        int k = nRun++;
        while (k > 0 && drums_[(size_t) runOrder_[(size_t) k - 1]].ringingSlots() < c) { runOrder_[(size_t) k] = runOrder_[(size_t) k - 1]; --k; }
        runOrder_[(size_t) k] = d;
    }
    jobN_ = n;
    if (pool_ != nullptr && nRun >= 2)
        pool_->run(nRun, runOrder_.data(), &Kit::renderDrumJob, this);
    else
        for (int k = 0; k < nRun; ++k) renderDrumJob(this, runOrder_[(size_t) k]);
    for (int k = 0; k < nQuiet; ++k) renderDrumJob(this, quiet_[(size_t) k]);

    std::fill(mix_.begin(), mix_.begin() + n, 0.0);
    for (size_t d = 0; d < drums_.size(); ++d)
    {
        const double* o = outs_[d].data();
        for (int i = 0; i < n; ++i) mix_[(size_t) i] += o[i];
    }
    if (buzz_ > 0)
    {
        // Sympathetic snare buzz: the rest of the kit drives each snare's head modes (air coupling);
        // when the head motion passes the wires' seating level, the wires rattle.
        const double ka = std::exp(-1 / (0.0005 * sr_)), kr = std::exp(-1 / (0.03 * sr_));
        const double thr = 0.004;
        for (auto& s : sym_)
        {
            Drum& dr = drums_[(size_t) s.drum];
            const double* own = outs_[(size_t) s.drum].data();
            bool any = false;
            for (int i = 0; i < n && ! any; ++i) any = (mix_[(size_t) i] - own[i]) != 0.0;
            const size_t M = s.F.size();
            if (! any)
            {
                for (size_t j = 0; j < M; ++j) s.y[j] *= std::exp(C(-s.D[j], 2 * kPi * s.F[j]) * ((double) n / sr_));
                s.env *= std::exp(-n / (0.03 * sr_));
                continue;
            }
            const double tune = dr.tuneRatio();
            for (size_t j = 0; j < M; ++j)
            {
                s.c[j] = buzz_ * 0.00017 * s.w[j];
                s.p[j] = std::exp(C(-s.D[j] * dr.decayMul(s.F[j]), 2 * kPi * s.F[j] * tune) / sr_);
            }
            // driven head modes, SIMD across modes
            double* yr = s.yr.data(); double* yi = s.yi.data(); double* pr = s.pr.data(); double* pi = s.pi.data();
            const double* cc = s.c.data();
            for (size_t j = 0; j < M; ++j) { yr[j] = s.y[j].real(); yi[j] = s.y[j].imag(); pr[j] = s.p[j].real(); pi[j] = s.p[j].imag(); }
            const int Mi = (int) M;
            for (int i = 0; i < n; ++i)
            {
                const double x = mix_[(size_t) i] - own[i];
                double acc = 0;
#pragma omp simd reduction(+ : acc)
                for (int j = 0; j < Mi; ++j)
                {
                    const double re = cc[j] * x + pr[j] * yr[j] - pi[j] * yi[j];
                    const double im = pr[j] * yi[j] + pi[j] * yr[j];
                    yr[j] = re; yi[j] = im;
                    acc += re;
                }
                head_[(size_t) i] = acc;
            }
            for (size_t j = 0; j < M; ++j) s.y[j] = { yr[j], yi[j] };
            double e = s.env, mx = 0;
            double* ownOut = outs_[(size_t) s.drum].data();
            for (int i = 0; i < n; ++i)
            {
                mix_[(size_t) i] += head_[(size_t) i];
                ownOut[i] += head_[(size_t) i];           // the snare's own sympathetic ring (after its drive was taken)
                const double a = std::fabs(head_[(size_t) i]) / s.href;
                e = a > e ? ka * e + (1 - ka) * a : kr * e + (1 - kr) * a;
                const double dv = std::pow(clampd((e - thr) / (0.25 - thr), 0, 1), 0.7);
                drive_[(size_t) i] = dv;
                mx = std::max(mx, dv);
            }
            s.env = e;
            if (mx < 1e-6 || noiseless_) continue;
            std::fill(wire_.begin(), wire_.begin() + n, 0.0);
            s.wires.processCommon(wire_.data(), n, drive_.data());
            for (int i = 0; i < n; ++i)
            {
                mix_[(size_t) i] += wire_[(size_t) i];
                ownOut[i] += wire_[(size_t) i];
            }
        }
    }
    if (out) std::copy(mix_.begin(), mix_.begin() + n, out);
    if (L && R)
    {
        std::fill(L, L + n, 0.0);
        std::fill(R, R + n, 0.0);
        const double inv = 1.0 / n;
        for (size_t d = 0; d < drums_.size(); ++d)
        {
            const double l0 = gainL_[d], r0 = gainR_[d], dl = (targetL_[d] - l0) * inv, dr = (targetR_[d] - r0) * inv;
            gainL_[d] = targetL_[d];
            gainR_[d] = targetR_[d];
            if (l0 == 0 && r0 == 0 && dl == 0 && dr == 0) continue;
            const double* o = outs_[d].data();
            for (int i = 0; i < n; ++i)
            {
                L[i] += (l0 + dl * (i + 1)) * o[i];
                R[i] += (r0 + dr * (i + 1)) * o[i];
            }
        }
    }
}

} // namespace tympanis
