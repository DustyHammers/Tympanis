// tympanis_hosttest — drives the plugin's processor the way a host does (no audio device).
//
//   tympanis_hosttest checks           run the processor checks (exit code 1 on failure)
//   tympanis_hosttest bench            CPU use with every drum ringing (prints a table)
//   tympanis_hosttest render SEQ OUT   play SEQ (JSON: sample_rate, duration, block,
//                                      events [[time_s, row_id, velocity 1..127], ...])
//                                      through processBlock and write a 24-bit WAV
#include "PluginProcessor.h"
#include "dsp/Json.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <map>

using namespace tympanis;

namespace
{
int failures = 0;
void check(bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (! ok) ++failures;
}

struct Host
{
    std::unique_ptr<TympanisProcessor> p;
    double sr;
    int block;
    Host(double sampleRate, int blockSize) : sr(sampleRate), block(blockSize)
    {
        p.reset(static_cast<TympanisProcessor*>(createPluginFilter()));
        p->setPlayConfigDetails(0, 2, sr, block);
        p->prepareToPlay(sr, block);
    }
    // Plays (sample, note, velocity) events for n samples in blocks of `blk`; returns the left channel.
    std::vector<float> play(const std::vector<std::tuple<int64_t, int, int>>& ev, int64_t n, int blk, int channel = 10)
    {
        std::vector<float> out((size_t) n);
        juce::AudioBuffer<float> buf(2, blk);
        size_t ei = 0;
        for (int64_t s = 0; s < n; s += blk)
        {
            const int len = (int) std::min<int64_t>(blk, n - s);
            buf.setSize(2, len, false, false, true);
            juce::MidiBuffer midi;
            while (ei < ev.size() && std::get<0>(ev[ei]) < s + len)
            {
                midi.addEvent(juce::MidiMessage::noteOn(channel, std::get<1>(ev[ei]), (juce::uint8) std::get<2>(ev[ei])),
                              (int) (std::get<0>(ev[ei]) - s));
                ++ei;
            }
            p->processBlock(buf, midi);
            std::copy(buf.getReadPointer(0), buf.getReadPointer(0) + len, out.begin() + s);
        }
        return out;
    }
};

double peak(const std::vector<float>& x, size_t a = 0, size_t b = SIZE_MAX)
{
    double m = 0;
    for (size_t i = a; i < std::min(b, x.size()); ++i) m = std::max(m, (double) std::fabs(x[i]));
    return m;
}
bool finite(const std::vector<float>& x)
{
    for (float v : x) if (! std::isfinite(v)) return false;
    return true;
}

int runChecks()
{
    std::cout << "Processor checks\n";
    {
        Host h(44100, 256);
        check(h.p->isKitLoaded(), "baked kit loads" + (h.p->isKitLoaded() ? juce::String() : ": " + h.p->getLoadError()));
        check(h.p->getParameters().size() == 35, "35 host parameters (17 x tune, 17 x lug detune, snare buzz): " + juce::String(h.p->getParameters().size()));
        check(h.p->getMidiChannel() == kDefaultMidiChannel, "default MIDI channel " + juce::String(kDefaultMidiChannel));
        // every row plays on its default note
        for (size_t r = 0; r < kRows.size(); ++r)
        {
            Host one(44100, 256);
            const auto y = one.play({ { 100, kRows[r].defaultNote, 127 } }, 22050, 256);
            check(finite(y) && peak(y) > 0.01, juce::String("row ") + kRows[r].name + " plays (peak " + juce::String(peak(y), 3) + ")");
        }
        // other channels are ignored
        Host other(44100, 256);
        const auto y = other.play({ { 100, 38, 127 } }, 4410, 256, 1);
        check(peak(y) < 1e-30, "a note on channel 1 plays nothing");
    }
    {
        // block sizes: the same hits must give (nearly) the same audio at any host block size
        std::vector<std::tuple<int64_t, int, int>> ev;
        for (int k = 0; k < 24; ++k) ev.emplace_back(1000 + k * 3001, kRows[(size_t) (k * 7) % kRows.size()].defaultNote, 40 + (k * 13) % 88);
        std::vector<std::vector<float>> outs;
        for (int blk : { 1, 7, 64, 333, 512, 4096 })
        {
            Host h(48000, 512);
            outs.push_back(h.play(ev, 96000, blk));
            check(finite(outs.back()), "block size " + juce::String(blk) + ": finite output");
        }
        // the bank's 32-sample coefficient blocks and the noise draws follow the host's block
        // split, so compare the level (RMS in 10 ms windows) rather than the waveform
        auto rmsDb = [](const std::vector<float>& x, size_t a, size_t w) {
            double e = 0;
            for (size_t i = a; i < a + w; ++i) e += (double) x[i] * x[i];
            return 10 * std::log10(e / (double) w + 1e-30);
        };
        double worst = 0;
        const size_t w = 4800;   // 100 ms
        for (size_t i = 1; i < outs.size(); ++i)
            for (size_t a = 0; a + w <= outs[0].size(); a += w)
            {
                const double r = rmsDb(outs[0], a, w);
                if (r > -50) worst = std::max(worst, std::fabs(rmsDb(outs[i], a, w) - r));
            }
        check(worst < 0.5, "block sizes 1..4096: same level in every 100 ms window above -50 dBFS (max difference " + juce::String(worst, 3) + " dB)");
    }
    {
        // saved state: parameters, MIDI channel and notes
        Host a(44100, 256);
        a.p->setMidiChannel(3);
        a.p->setRowNote(0, 70);
        a.p->setRowNote(5, kNoteNone);
        if (auto* prm = a.p->params.getParameter("tom2_tune")) prm->setValueNotifyingHost(prm->convertTo0to1(-1.25f));
        if (auto* prm = a.p->params.getParameter("snare1_lug_detune")) prm->setValueNotifyingHost(0.75f);
        if (auto* prm = a.p->params.getParameter("snare_buzz")) prm->setValueNotifyingHost(0.2f);
        juce::MemoryBlock mb;
        a.p->getStateInformation(mb);
        Host b(44100, 256);
        b.p->setStateInformation(mb.getData(), (int) mb.getSize());
        check(b.p->getMidiChannel() == 3, "state: MIDI channel restored");
        check(b.p->getRowNote(0) == 70 && b.p->getRowNote(5) == kNoteNone && b.p->getRowNote(2) == kRows[2].defaultNote, "state: notes restored (incl. NONE)");
        check(std::abs(b.p->params.getRawParameterValue("tom2_tune")->load() + 1.25f) < 1e-3f, "state: tom 2 tune restored");
        check(std::abs(b.p->params.getRawParameterValue("snare1_lug_detune")->load() - 0.75f) < 1e-3f, "state: snare 1 lug detune restored");
        check(std::abs(b.p->params.getRawParameterValue("snare_buzz")->load() - 0.2f) < 1e-3f, "state: snare buzz restored");
        const auto y = b.play({ { 10, 70, 127 } }, 4410, 256, 3);
        check(peak(y) > 0.01, "state: kick 1 plays on its new note and channel");
        Host c(44100, 256);
        c.p->setStateInformation(mb.getData(), (int) mb.getSize());
        const auto z = c.play({ { 10, kRows[5].defaultNote, 127 } }, 4410, 256, 3);
        check(peak(z) < 1e-30, "state: a row set to NONE plays nothing");
    }
    {
        // tail: the kit falls silent
        Host h(44100, 512);
        const auto y = h.play({ { 0, 41, 127 }, { 0, 36, 127 }, { 0, 64, 127 } }, 44100 * 30, 512);
        check(peak(y, y.size() - 44100) < 1e-5, "silent 29 s after a hit (" + juce::String(20 * std::log10(peak(y, y.size() - 44100) + 1e-30), 1) + " dBFS)");
    }
    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}

int runBench()
{
    std::cout << "CPU with every drum and articulation struck every 250 ms (all 17 drums ringing, snare buzz 0.5)\n";
    std::cout << "sample rate  block   x real time   % of one core\n";
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int blk : { 64, 256 })
        {
            Host h(sr, blk);
            const int64_t n = (int64_t) (sr * 20);
            std::vector<std::tuple<int64_t, int, int>> ev;
            for (int64_t t = 0, k = 0; t < n; t += (int64_t) (sr * 0.25), ++k)
                for (size_t r = 0; r < kRows.size(); r += 1)
                    if ((r + (size_t) k) % 2 == 0) ev.emplace_back(t, kRows[r].defaultNote, 64 + (int) ((r * 17 + (size_t) k) % 64));
            const auto t0 = std::chrono::steady_clock::now();
            h.play(ev, n, blk);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("%8.0f   %5d   %10.1f   %11.1f\n", sr, blk, 20.0 / secs, 100.0 * secs / 20.0);
        }
    return 0;
}

int runRender(const std::string& seqPath, const std::string& outPath)
{
    const auto seq = json::parseFile(seqPath);
    const double sr = seq.at("sample_rate").number();
    const int blk = (int) seq.at("block").number();
    const int64_t n = (int64_t) (seq.at("duration").number() * sr);
    std::map<std::string, int> note;
    for (const auto& r : kRows) note[r.id] = r.defaultNote;
    Host h(sr, blk);
    if (const auto* ps = seq.find("params"))
        for (const auto& [id, v] : ps->object())
            if (auto* prm = h.p->params.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1((float) v.number()));
    std::vector<std::tuple<int64_t, int, int>> ev;
    for (const auto& e : seq.at("events").array())
    {
        const auto& a = e.array();
        ev.emplace_back((int64_t) (a[0].number() * sr), note.at(a[1].string()), (int) a[2].number());
    }
    std::stable_sort(ev.begin(), ev.end(), [](auto& x, auto& y) { return std::get<0>(x) < std::get<0>(y); });
    const auto y = h.play(ev, n, blk);
    juce::AudioBuffer<float> buf(1, (int) n);
    std::copy(y.begin(), y.end(), buf.getWritePointer(0));
    juce::File f(outPath);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(f), sr, 1, 24, {}, 0));
    if (! w) { std::cerr << "cannot write " << outPath << "\n"; return 1; }
    w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    std::cout << "peak " << juce::Decibels::gainToDecibels(peak(y)) << " dBFS\n";
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const std::string mode = argc > 1 ? argv[1] : "checks";
    if (mode == "checks") return runChecks();
    if (mode == "bench") return runBench();
    if (mode == "render" && argc > 3) return runRender(argv[2], argv[3]);
    std::cerr << "usage: tympanis_hosttest checks | bench | render SEQ.json OUT.wav\n";
    return 2;
}
