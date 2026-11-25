// tympanis_hosttest — drives the plugin's processor the way a host does (no audio device).
//
//   tympanis_hosttest checks           run the processor checks (exit code 1 on failure)
//   tympanis_hosttest bench            CPU use with every drum ringing (prints a table)
//   tympanis_hosttest render SEQ OUT   play SEQ (JSON: sample_rate, duration, block,
//                                      events [[time_s, row_id, velocity 1..127], ...])
//                                      through processBlock and write a 24-bit stereo WAV
//                                      (optional "params": { parameter_id: value, ... })
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
    std::vector<float> lastRight;
    void set(const char* id, float v)
    {
        auto* prm = p->params.getParameter(id);
        if (prm == nullptr) { std::cout << "  FAIL  no parameter " << id << "\n"; ++failures; return; }
        prm->setValueNotifyingHost(prm->convertTo0to1(v));
    }
    // Plays (sample, note, velocity) events for n samples in blocks of `blk`; returns the left
    // channel (the right one is in lastRight).
    std::vector<float> play(const std::vector<std::tuple<int64_t, int, int>>& ev, int64_t n, int blk, int channel = 10)
    {
        std::vector<float> out((size_t) n);
        lastRight.assign((size_t) n, 0.0f);
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
            std::copy(buf.getReadPointer(1), buf.getReadPointer(1) + len, lastRight.begin() + s);
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
        check(h.p->getParameters().size() == 136,
              "136 host parameters (17 x tune, lug detune, decay, size, level, pan, velocity sensitivity; 15 x strike position; snare buzz; main volume): "
                  + juce::String(h.p->getParameters().size()));
        check(h.p->params.getParameter("kick1_strike_pos") == nullptr && h.p->params.getParameter("tom1_strike_pos") != nullptr,
              "no strike position on the bass drums");
        check(h.p->getMidiChannel() == kDefaultMidiChannel, "default MIDI channel " + juce::String(kDefaultMidiChannel));
        {
            // Size is the shell's depth: -4 .. +2" in 1" steps (Kris, 2026-11-25)
            auto* t3 = h.p->params.getParameter("tom3_size");
            const auto r = t3->getNormalisableRange();
            check(std::abs(t3->convertFrom0to1(t3->getDefaultValue()) - 10.0f) < 1e-3f && std::abs(r.start - 6.0f) < 1e-3f
                      && std::abs(r.end - 12.0f) < 1e-3f && std::abs(r.interval - 1.0f) < 1e-3f,
                  "Tom 3 Size is its shell depth: 10\" (6\" .. 12\", 1\" steps)");
            auto* s4 = h.p->params.getParameter("snare4_size");
            check(std::abs(s4->convertFrom0to1(s4->getDefaultValue()) - 5.0f) < 1e-3f, "Snare 4 Size: its 5\" depth");
        }
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
        // a note belongs to one row
        Host h(44100, 256);
        h.p->setRowNote(3, 38);                      // snare 1 rim shot takes snare 1's note
        check(h.p->getRowNote(3) == 38 && h.p->getRowNote(2) == kNoteNone, "a note given to another row moves (the old row is set to none)");
        const auto y = h.play({ { 10, 38, 127 } }, 8820, 256);
        check(peak(y) > 0.01, "the moved note plays its new row");
        // a saved state where two rows share a note: the first row keeps it
        juce::ValueTree root("TympanisState");
        root.appendChild(h.p->params.copyState(), nullptr);
        juce::ValueTree map("MidiMap");
        map.setProperty("channel", 10, nullptr);
        map.setProperty("tom1", 50, nullptr);
        map.setProperty("tom2", 50, nullptr);
        root.appendChild(map, nullptr);
        juce::MemoryBlock mb;
        h.p->copyXmlToBinary(*root.createXml(), mb);
        Host b(44100, 256);
        b.p->setStateInformation(mb.getData(), (int) mb.getSize());
        check(b.p->getRowNote(14) == 50 && b.p->getRowNote(16) == kNoteNone, "a saved state with a shared note: the first row keeps it");
    }
    {
        // Velocity Sensitivity (as Cymbalis): v = vf + s (v_in - vf), vf = 100/127
        const int note = kRows[2].defaultNote;    // snare 1
        auto hit = [note](float sens, int vel) {
            Host h(44100, 256);
            h.set("snare_buzz", 0.0f);
            h.set("snare1_vel_sens", sens);
            return h.play({ { 10, note, vel } }, 22050, 256);
        };
        Host d(44100, 256);
        auto* vs = d.p->params.getParameter("snare1_vel_sens");
        check(vs != nullptr && std::abs(vs->getDefaultValue() - 1.0f) < 1e-6f && vs->getText(vs->getDefaultValue(), 8) == "100%",
              "velocity sensitivity per drum, default 100%");
        const double full127 = peak(hit(1.0f, 127)), full40 = peak(hit(1.0f, 40));
        const auto zero1 = hit(0.0f, 1), zero127 = hit(0.0f, 127), full100 = hit(1.0f, 100);
        double diff = 0;
        for (size_t i = 0; i < zero1.size(); ++i) diff = std::max(diff, (double) std::abs(zero1[i] - zero127[i]));
        check(diff < 1e-6 * peak(zero127), "velocity sensitivity 0%: velocity 1 and 127 play the same");
        double d100 = 0;
        for (size_t i = 0; i < zero1.size(); ++i) d100 = std::max(d100, (double) std::abs(zero127[i] - full100[i]));
        check(d100 < 1e-6 * peak(full100), "velocity sensitivity 0%: every hit plays as velocity 100");
        const double r100 = 20 * std::log10(full127 / full40);
        const double r50 = 20 * std::log10(peak(hit(0.5f, 127)) / peak(hit(0.5f, 40)));
        check(r100 > 6.0 && r50 > 0.5 && r50 < r100 - 1.0,
              "velocity sensitivity narrows the dynamics (velocity 127 vs 40: " + juce::String(r100, 1) + " dB at 100%, "
                  + juce::String(r50, 1) + " dB at 50%)");
        Host o(44100, 256);
        o.set("kick1_vel_sens", 0.0f);
        o.p->clearHits();
        o.play({ { 10, kRows[0].defaultNote, 20 }, { 300, kRows[2].defaultNote, 20 } }, 1024, 256);
        TympanisProcessor::HitEvent e1 {}, e2 {};
        const bool got = o.p->popHit(e1) && o.p->popHit(e2);
        check(got && std::abs(e1.velocity - 100.0f / 127.0f) < 1e-5f && std::abs(e2.velocity - 20.0f / 127.0f) < 1e-5f,
              "velocity sensitivity is per drum, and the editor sees the velocity played");
    }
    {
        // mixer: level, pan, main volume
        const int note = kRows[2].defaultNote;    // snare 1
        Host ref(44100, 256);
        ref.set("snare_buzz", 0.0f);
        const auto y0 = ref.play({ { 10, note, 127 } }, 22050, 256);
        const double p0 = peak(y0);
        check(std::abs(peak(y0) - peak(ref.lastRight)) < 1e-6 * p0, "centre pan: left = right");
        Host off(44100, 256);
        off.set("snare1_level", -60.0f);
        off.set("snare_buzz", 0.0f);                 // else snares 2..4 buzz along, on their own channels
        check(peak(off.play({ { 10, note, 127 } }, 22050, 256)) < 1e-30, "level at the bottom (-inf): the drum is silent");
        Host left(44100, 256);
        left.set("snare1_pan", -1.0f);
        left.set("snare_buzz", 0.0f);
        const auto yl = left.play({ { 10, note, 127 } }, 22050, 256);
        check(peak(left.lastRight) < 1e-6 * p0 && std::abs(peak(yl) / p0 - std::sqrt(2.0)) < 0.15,
              "hard left: right silent, left +3 dB (left " + juce::String(20 * std::log10(peak(yl) / p0), 2) + " dB)");
        Host vol(44100, 256);
        vol.set("main_volume", -6.0f);
        vol.set("snare_buzz", 0.0f);
        vol.set("snare1_level", -6.0f);
        const double r = peak(vol.play({ { 10, note, 127 } }, 22050, 256)) / p0;
        check(std::abs(20 * std::log10(r) + 12.0) < 1.0, "main volume -6 dB and level -6 dB: -12 dB (" + juce::String(20 * std::log10(r), 2) + " dB)");
    }
    {
        // drum controls act on the sound
        auto crossings = [](const std::vector<float>& x, size_t a, size_t b) {
            int c = 0;
            for (size_t i = a + 1; i < b; ++i) c += (x[i - 1] < 0) != (x[i] < 0);
            return c;
        };
        auto energy = [](const std::vector<float>& x, size_t a, size_t b) {
            double e = 0;
            for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i];
            return e;
        };
        const int tom3 = kRows[18].defaultNote;
        Host a(44100, 256), big(44100, 256), longd(44100, 256), centre(44100, 256), rim(44100, 256), shallow(44100, 256), sn(44100, 256), snSize(44100, 256);
        big.set("tom3_size", 12.0f);
        shallow.set("tom3_size", 6.0f);
        snSize.set("snare1_size", 2.5f);
        longd.set("tom3_decay", 3.0f);
        centre.set("tom3_strike_pos", 0.05f);
        rim.set("tom3_strike_pos", 0.85f);
        const auto ya = a.play({ { 0, tom3, 110 } }, 66150, 256);
        const auto yb = big.play({ { 0, tom3, 110 } }, 66150, 256);
        const auto yd = longd.play({ { 0, tom3, 110 } }, 66150, 256);
        const auto yc = centre.play({ { 0, tom3, 110 } }, 66150, 256);
        const auto yr = rim.play({ { 0, tom3, 110 } }, 66150, 256);
        const auto ys = shallow.play({ { 0, tom3, 110 } }, 66150, 256);
        // the ring after the attack: the fundamental's group (its pitch follows the depth)
        check(crossings(ys, 22050, 66150) > crossings(ya, 22050, 66150) && crossings(ya, 22050, 66150) > crossings(yb, 22050, 66150),
              "tom 3 depth 6\" / 10\" / 12\": its ring lower as the shell deepens (zero crossings " + juce::String(crossings(ys, 22050, 66150)) + ", "
                  + juce::String(crossings(ya, 22050, 66150)) + ", " + juce::String(crossings(yb, 22050, 66150)) + ")");
        {
            // Size does nothing on the other drums (greyed out in the editor)
            const int s1 = kRows[2].defaultNote;
            const auto y0 = sn.play({ { 0, s1, 110 } }, 22050, 256);
            const auto y1 = snSize.play({ { 0, s1, 110 } }, 22050, 256);
            check(y0 == y1, "snare 1 Size changes nothing (its depth does not play)");
        }
        check(energy(yd, 22050, 66150) > 2.0 * energy(ya, 22050, 66150), "tom 3 decay 3x: more sound 0.5..1.5 s after the hit");
        check(crossings(yr, 441, 22050) > crossings(yc, 441, 22050),
              "tom 3 strike near the rim is brighter than at the centre (zero crossings " + juce::String(crossings(yr, 441, 22050)) + " vs " + juce::String(crossings(yc, 441, 22050)) + ")");
    }
    {
        // tail: the kit falls silent
        Host h(44100, 512);
        const auto y = h.play({ { 0, 41, 127 }, { 0, 36, 127 }, { 0, 64, 127 } }, 44100 * 30, 512);
        check(peak(y, y.size() - 44100) < 1e-5, "silent 29 s after a hit (" + juce::String(20 * std::log10(peak(y, y.size() - 44100) + 1e-30), 1) + " dBFS)");
    }
    {
        // the editor's hits: one event per strike, the row and velocity
        Host h(44100, 256);
        h.p->clearHits();
        h.play({ { 10, kRows[18].defaultNote, 100 }, { 300, kRows[19].defaultNote, 64 }, { 400, 120, 127 } }, 2048, 256);
        TympanisProcessor::HitEvent e {};
        std::vector<TympanisProcessor::HitEvent> got;
        while (h.p->popHit(e)) got.push_back(e);
        check(got.size() == 2 && got[0].row == 18 && got[1].row == 19 && std::abs(got[0].velocity - 100.0f / 127.0f) < 1e-3f,
              "hits for the editor: one per strike, with its row and velocity (" + juce::String((int) got.size()) + " events)");
        // a full FIFO drops events, never blocks
        std::vector<std::tuple<int64_t, int, int>> many;
        for (int i = 0; i < 100; ++i) many.push_back({ i, kRows[0].defaultNote, 100 });
        h.play(many, 256, 256);
        int n = 0;
        while (h.p->popHit(e)) ++n;
        check(n == TympanisProcessor::hitFifoSize, "hits: a full FIFO keeps " + juce::String(TympanisProcessor::hitFifoSize) + " (" + juce::String(n) + ")");
    }
    {
        // MIDI learn: the next note-on goes to the row, and plays it
        Host h(44100, 256);
        h.p->startMidiLearn(20);
        const auto y = h.play({ { 10, 99, 127 } }, 4410, 256);
        check(h.p->getRowNote(20) == 99 && h.p->midiLearnRow() == -1 && peak(y) > 0.01, "MIDI learn: the next note goes to the row and plays it");
        h.p->startMidiLearn(20);
        h.play({ { 10, 98, 127 } }, 256, 256, 1);
        check(h.p->getRowNote(20) == 99 && h.p->midiLearnRow() == 20, "MIDI learn: a note on another channel is ignored");
        h.p->cancelMidiLearn();
    }
    {
        // the editor's state: page, drum and size are saved with the session
        Host a(44100, 256);
        a.p->setEditorPage(2);
        a.p->setSelectedDrum(11);
        a.p->setEditorSize(1200, 800);
        juce::MemoryBlock mb;
        a.p->getStateInformation(mb);
        Host b(44100, 256);
        b.p->setStateInformation(mb.getData(), (int) mb.getSize());
        check(b.p->editorPage() == 2 && b.p->selectedDrum() == 11 && b.p->editorSize() == juce::Point<int>(1200, 800),
              "state: the editor's page, drum and size restored");
        // the editor opens, on the saved page, and closes
        std::unique_ptr<juce::AudioProcessorEditor> ed(b.p->createEditor());
        check(ed != nullptr && ed->getWidth() == 1200 && ed->getHeight() == 800, "the editor opens at the saved size");
        ed.reset();
        check(b.p->getParameters().size() == 136, "the editor adds no parameters (136)");
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
    juce::AudioBuffer<float> buf(2, (int) n);
    std::copy(y.begin(), y.end(), buf.getWritePointer(0));
    std::copy(h.lastRight.begin(), h.lastRight.end(), buf.getWritePointer(1));
    juce::File f(outPath);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(f), sr, 2, 24, {}, 0));
    if (! w) { std::cerr << "cannot write " << outPath << "\n"; return 1; }
    w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    std::cout << "peak " << juce::Decibels::gainToDecibels(std::max(peak(y), peak(h.lastRight))) << " dBFS\n";
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
