// Filters.h — noise sources and band filters for the engine (no JUCE, real-time safe).
#pragma once

#include <cmath>
#include <complex>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace tympanis
{

constexpr double kPi = 3.14159265358979323846;

// xoshiro256+ — fast uniform source. gauss() is a unit-variance sum of four uniforms (white,
// near-Gaussian; it only ever feeds band filters, whose output is Gaussian either way).
class Noise
{
public:
    explicit Noise(uint64_t seed = 1) { reseed(seed); }

    void reseed(uint64_t seed)
    {
        for (auto& w : s_)
        {
            seed += 0x9E3779B97F4A7C15ull;     // splitmix64
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            w = z ^ (z >> 31);
        }
    }

    uint64_t next() noexcept
    {
        const uint64_t r = s_[0] + s_[3];
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0]; s_[3] ^= s_[1]; s_[1] ^= s_[2]; s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = (s_[3] << 45) | (s_[3] >> 19);
        return r;
    }

    // 0 .. 1 (53 bits)
    double uniform() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }

    // Unit-variance white noise: four 16-bit uniforms from one draw, summed, centred and scaled.
    double gauss() noexcept
    {
        const uint64_t r = next();
        const double s = (double) (r & 0xFFFF) + (double) ((r >> 16) & 0xFFFF)
                       + (double) ((r >> 32) & 0xFFFF) + (double) (r >> 48);
        // each term: mean 32767.5, variance (65536^2 - 1) / 12
        return (s - 4.0 * 32767.5) * (1.0 / 37837.22);   // sqrt(4 * (65536^2 - 1) / 12)
    }

    // A proper standard normal (Box-Muller), for the per-hit variation draws.
    double normal() noexcept
    {
        if (haveSpare_) { haveSpare_ = false; return spare_; }
        double u1 = uniform(), u2 = uniform();
        if (u1 < 1e-300) u1 = 1e-300;
        const double r = std::sqrt(-2.0 * std::log(u1)), a = 6.283185307179586 * u2;
        spare_ = r * std::sin(a); haveSpare_ = true;
        return r * std::cos(a);
    }

private:
    uint64_t s_[4] {};
    double spare_ = 0;
    bool haveSpare_ = false;
};

struct Biquad
{
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    // transposed direct form II (as scipy's sosfilt)
    double tick(double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
};

// Fourth-order Butterworth band-pass (order-2 prototype), designed the way
// scipy.signal.butter(2, [lo, hi], 'band', fs=sr, output='sos') designs it.
struct BandPass
{
    Biquad s[2];
    double tick(double x) noexcept { return s[1].tick(s[0].tick(x)); }
    void reset() noexcept { s[0].reset(); s[1].reset(); }

    void design(double lo, double hi, double sr)
    {
        using C = std::complex<double>;
        const double fs2 = 4.0;                                   // scipy works at fs = 2
        const double w1 = 4.0 * std::tan(kPi * lo / sr), w2 = 4.0 * std::tan(kPi * hi / sr);
        const double bw = w2 - w1, wo = std::sqrt(w1 * w2);
        const C plp = C(-std::sqrt(0.5), std::sqrt(0.5)) * (bw / 2);   // one of the conjugate prototype poles
        const C root = std::sqrt(plp * plp - wo * wo);
        const C pa[2] = { plp + root, plp - root };               // with their conjugates: the four poles
        C den = 1;
        for (const C& p : pa) den *= (fs2 - p) * (fs2 - std::conj(p));
        const double k = (bw * bw * fs2 * fs2 / den).real();
        for (int i = 0; i < 2; ++i)
        {
            const C pz = (fs2 + pa[i]) / (fs2 - pa[i]);
            s[i].b0 = 1; s[i].b1 = 0; s[i].b2 = -1;
            s[i].a1 = -2 * pz.real(); s[i].a2 = std::norm(pz);
        }
        s[0].b0 *= k; s[0].b2 *= k;
        reset();
    }

    // 1 / RMS gain for unit white noise: 1 / sqrt(sum h^2), exact, from the state-space
    // controllability Gramian (Lyapunov sum by doubling). Allocation-free and cheap enough for
    // the audio thread (a live size change re-designs the bands).
    double whiteNoiseNorm(double /*sampleRate*/ = 0) const
    {
        const Biquad& p = s[0];
        const Biquad& q = s[1];
        // states [z1, z2, w1, w2] of the two transposed-direct-form sections in cascade
        const double c1 = q.b1 - q.a1 * q.b0, c2 = q.b2 - q.a2 * q.b0;
        double A[4][4] = { { -p.a1, 1, 0, 0 }, { -p.a2, 0, 0, 0 }, { c1, 0, -q.a1, 1 }, { c2, 0, -q.a2, 0 } };
        const double B[4] = { p.b1 - p.a1 * p.b0, p.b2 - p.a2 * p.b0, c1 * p.b0, c2 * p.b0 };
        const double Cv[4] = { q.b0, 0, 1, 0 }, D = q.b0 * p.b0;
        double P[4][4], T[4][4], M2[4][4];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) P[i][j] = B[i] * B[j];
        for (int it = 0; it < 64; ++it)
        {
            // P += A P A^T ; A = A A
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                {
                    double v = 0;
                    for (int k = 0; k < 4; ++k) v += A[i][k] * P[k][j];
                    T[i][j] = v;
                }
            double add = 0, tot = 0;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                {
                    double v = 0;
                    for (int k = 0; k < 4; ++k) v += T[i][k] * A[j][k];
                    P[i][j] += v;
                    add += std::fabs(v); tot += std::fabs(P[i][j]);
                }
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                {
                    double v = 0;
                    for (int k = 0; k < 4; ++k) v += A[i][k] * A[k][j];
                    M2[i][j] = v;
                }
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) A[i][j] = M2[i][j];
            if (add < 1e-18 * tot) break;
        }
        double e = D * D;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) e += Cv[i] * P[i][j] * Cv[j];
        return 1.0 / std::sqrt(e);
    }
};

// A set of BandPass filters, each fed its own white noise, run side by side (SIMD across bands).
// Each band has its own xorshift32 pair; gauss-like noise = four 16-bit uniforms summed.
class NoiseBank
{
public:
    void resize(int nb, uint32_t seed)
    {
        n_ = nb;
        for (auto* v : { &b0a_, &b2a_, &a1a_, &a2a_, &b0b_, &b2b_, &a1b_, &a2b_, &z1a_, &z2a_, &z1b_, &z2b_, &gain_ })
            v->assign((size_t) nb, 0.0);
        sa_.assign((size_t) nb, 0); sb_.assign((size_t) nb, 0);
        Noise r(seed);
        for (int j = 0; j < nb; ++j)
        {
            sa_[(size_t) j] = (uint32_t) (r.next() >> 32) | 1u;
            sb_[(size_t) j] = (uint32_t) (r.next() >> 32) | 1u;
        }
    }
    int size() const { return n_; }
    void setBand(int j, const BandPass& f, double gain)
    {
        const size_t i = (size_t) j;
        b0a_[i] = f.s[0].b0; b2a_[i] = f.s[0].b2; a1a_[i] = f.s[0].a1; a2a_[i] = f.s[0].a2;
        b0b_[i] = f.s[1].b0; b2b_[i] = f.s[1].b2; a1b_[i] = f.s[1].a1; a2b_[i] = f.s[1].a2;
        gain_[i] = gain;
    }
    double gain(int j) const { return gain_[(size_t) j]; }
    void reset()
    {
        for (auto* v : { &z1a_, &z2a_, &z1b_, &z2b_ }) std::fill(v->begin(), v->end(), 0.0);
    }

    // Exponentially decaying band levels: out[i] += sum_j y_j * gain_j * env_j; env_j *= r_j each sample.
    void processDecay(double* out, int n, double* env, const double* r)
    {
        run(out, n, [&](int, int j) { const double e = env[j]; env[j] = e * r[j]; return e; }, nullptr);
    }
    // Per-band, per-sample levels: env[j * stride + i].
    void processEnv(double* out, int n, const double* env, int stride)
    {
        run(out, n, [&](int i, int j) { return env[(size_t) j * (size_t) stride + (size_t) i]; }, nullptr);
    }
    // One level for all bands: out[i] += drive[i] * sum_j y_j * gain_j.
    void processCommon(double* out, int n, const double* drive)
    {
        run(out, n, [](int, int) { return 1.0; }, drive);
    }

private:
    template <class Level>
    void run(double* out, int n, Level level, const double* drive)
    {
        double* __restrict b0a = b0a_.data(); double* __restrict b2a = b2a_.data();
        double* __restrict a1a = a1a_.data(); double* __restrict a2a = a2a_.data();
        double* __restrict b0b = b0b_.data(); double* __restrict b2b = b2b_.data();
        double* __restrict a1b = a1b_.data(); double* __restrict a2b = a2b_.data();
        double* __restrict z1a = z1a_.data(); double* __restrict z2a = z2a_.data();
        double* __restrict z1b = z1b_.data(); double* __restrict z2b = z2b_.data();
        double* __restrict gn = gain_.data();
        uint32_t* __restrict sa = sa_.data(); uint32_t* __restrict sb = sb_.data();
        const int nb = n_;
        for (int i = 0; i < n; ++i)
        {
            double acc = 0;
#pragma omp simd reduction(+ : acc)
            for (int j = 0; j < nb; ++j)
            {
                uint32_t x = sa[j], y = sb[j];
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                y ^= y << 13; y ^= y >> 17; y ^= y << 5;
                sa[j] = x; sb[j] = y;
                const int32_t u = (int32_t) ((x & 0xFFFFu) + (x >> 16) + (y & 0xFFFFu) + (y >> 16)) - 131070;
                const double g = (double) u * (1.0 / 37837.22);
                const double y1 = b0a[j] * g + z1a[j];
                z1a[j] = z2a[j] - a1a[j] * y1;
                z2a[j] = b2a[j] * g - a2a[j] * y1;
                const double y2 = b0b[j] * y1 + z1b[j];
                z1b[j] = z2b[j] - a1b[j] * y2;
                z2b[j] = b2b[j] * y1 - a2b[j] * y2;
                acc += y2 * gn[j] * level(i, j);
            }
            out[i] += drive ? acc * drive[i] : acc;
        }
    }

    int n_ = 0;
    std::vector<double> b0a_, b2a_, a1a_, a2a_, b0b_, b2b_, a1b_, a2b_, z1a_, z2a_, z1b_, z2b_, gain_;
    std::vector<uint32_t> sa_, sb_;
};

} // namespace tympanis
