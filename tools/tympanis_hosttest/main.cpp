// tympanis_hosttest — drives the plugin's processor the way a host does (no audio device).
//
//   tympanis_hosttest checks           run the processor checks (exit code 1 on failure)
//   tympanis_hosttest bench [--seconds S] [--one-thread]
//                                      CPU use with every drum ringing: the one-thread table,
//                                      then per block, paced, at 1-3 threads (S s per case, 4);
//                                      --one-thread: the one-thread table only
//   tympanis_hosttest render SEQ OUT   play SEQ (JSON: sample_rate, duration, block,
//                                      events [[time_s, row_id, velocity 1..127], ...])
//                                      through processBlock and write a 24-bit stereo WAV
//                                      (optional "params": { parameter_id: value, ... };
//                                      "automation": [[time_s, parameter_id, value], ...],
//                                      each set before the block it falls in)
//                         [--raw OUT.f32]  also the float samples, L then R (md5sum)
//                         [--threads N]    the drums through a pool of N threads (test only)
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/EditorSize.h"
#include "dsp/Json.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>
#include <functional>
#include <algorithm>
#include <thread>
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
    // threads > 1: the drums through a pool (the processor's test-only setting, before prepare)
    Host(double sampleRate, int blockSize, int threads = 1, int timingSamples = 0) : sr(sampleRate), block(blockSize)
    {
        p.reset(static_cast<TympanisProcessor*>(createPluginFilter()));
        p->setRenderThreadsForTesting(threads, timingSamples);
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
    // onBlock(s), if set, is called before each block, with its first sample (parameter moves).
    std::vector<float> play(const std::vector<std::tuple<int64_t, int, int>>& ev, int64_t n, int blk, int channel = 10,
                            const std::function<void(int64_t)>& onBlock = {})
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
            if (onBlock) onBlock(s);
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

// A row's index in kRows by its ID.
int rowIndex(const char* id)
{
    for (size_t r = 0; r < kRows.size(); ++r)
        if (std::string(kRows[r].id) == id) return (int) r;
    std::cout << "  FAIL  no row " << id << "\n";
    ++failures;
    return 0;
}

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
        check(h.p->getParameters().size() == 124,
              "124 host parameters (15 x tune, lug detune, decay, size, level, pan, velocity sensitivity; 13 x strike position; "
              "2 x snares on/off, wire tension; snare buzz; main volume): "
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
            auto* t1 = h.p->params.getParameter("tom1_size");
            const auto r1 = t1->getNormalisableRange();
            check(std::abs(t1->convertFrom0to1(t1->getDefaultValue()) - 10.0f) < 1e-3f && std::abs(r1.start - 6.0f) < 1e-3f
                      && std::abs(r1.end - 12.0f) < 1e-3f,
                  "Tom 1 Size: 10\" x 10\" (6\" .. 12\")");
            auto* o4 = h.p->params.getParameter("octo4_size");
            check(std::abs(o4->convertFrom0to1(o4->getDefaultValue()) - 23.5f) < 1e-3f, "Octoban 4 Size: its 23.5\" length");
            auto* s4 = h.p->params.getParameter("snare4_size");
            check(std::abs(s4->convertFrom0to1(s4->getDefaultValue()) - 5.0f) < 1e-3f, "Popcorn Snare Size: its 5\" depth");
        }
        {
            // Snares On/Off (Kris, 2026-11-26): on the Snare and the Popcorn Snare only, default on;
            // Snare 2 and Snare 3 are no longer drums of the plugin
            int switches = 0;
            for (const auto& d : kDrums)
                switches += h.p->params.getParameter(juce::String(d.key) + "_snares") != nullptr ? 1 : 0;
            auto* sn = h.p->params.getParameter("snare1_snares");
            auto* pc = h.p->params.getParameter("snare4_snares");
            check(switches == 2 && sn != nullptr && pc != nullptr && sn->getDefaultValue() > 0.5f && pc->getDefaultValue() > 0.5f
                      && sn->getName(64) == "Snare Snares" && pc->getName(64) == "Popcorn Snare Snares",
                  "Snares On/Off on the Snare and the Popcorn Snare only, default on");
            check(h.p->params.getParameter("snare2_tune") == nullptr && h.p->params.getParameter("snare3_tune") == nullptr,
                  "no Snare 2 or Snare 3 parameters");
            // Snare Wire Tension (Kris, 2026-11-26): the same two drums, 0 tight (as recorded, default) .. 1 loose
            int wires = 0;
            for (const auto& d : kDrums)
                wires += h.p->params.getParameter(juce::String(d.key) + "_wire_tension") != nullptr ? 1 : 0;
            auto* wn = h.p->params.getParameter("snare1_wire_tension");
            auto* wp = h.p->params.getParameter("snare4_wire_tension");
            check(wires == 2 && wn != nullptr && wp != nullptr && wn->getDefaultValue() == 0.0f && wp->getDefaultValue() == 0.0f
                      && wn->getName(64) == "Snare Snare Wire Tension" && wp->getName(64) == "Popcorn Snare Snare Wire Tension",
                  "Snare Wire Tension on the Snare and the Popcorn Snare only, default tight (0)");
        }
        // every row plays on its default note
        for (size_t r = 0; r < kRows.size(); ++r)
        {
            Host one(44100, 256);
            const auto y = one.play({ { 100, kRows[r].defaultNote, 127 } }, 22050, 256);
            check(finite(y) && peak(y) > 0.01, juce::String("row ") + kRows[r].name + " plays (peak " + juce::String(peak(y), 3) + ")");
        }
        // Snares Off: the Snare and the Popcorn Snare play their wires-off models; a hit that rings
        // keeps ringing when the switch flips (it picks the model of the next hit only)
        for (const char* key : { "snare1", "snare4" })
        {
            int note = 0;
            for (const auto& r : kRows)
                if (juce::String(r.id) == key) note = r.defaultNote;
            Host on(44100, 256), off(44100, 256), flip(44100, 256);
            const juce::String sw = juce::String(key) + "_snares";
            off.set(sw.toRawUTF8(), 0.0f);
            const auto yOn = on.play({ { 100, note, 110 } }, 22050, 256);
            const auto yOff = off.play({ { 100, note, 110 } }, 22050, 256);
            const auto yFlip = flip.play({ { 100, note, 110 } }, 22050, 256, 10, [&](int64_t s) {
                if (s == 2560) flip.set(sw.toRawUTF8(), 0.0f);
            });
            check(peak(yOff) > 0.01 && yOn != yOff, juce::String(key) + " with its snares off plays its wires-off model");
            check(yFlip == yOn, juce::String(key) + ": flipping the switch leaves a ringing hit as it is");
            // Snare Wire Tension: looser rings longer with the snares on (the tail 0.2 .. 1 s up);
            // with them off it changes nothing (the wires-off model has no wires)
            Host loose(44100, 256), looseOff(44100, 256);
            const juce::String wt = juce::String(key) + "_wire_tension";
            loose.set(wt.toRawUTF8(), 1.0f);
            looseOff.set(wt.toRawUTF8(), 1.0f);
            looseOff.set(sw.toRawUTF8(), 0.0f);
            const auto yLoose = loose.play({ { 100, note, 110 } }, 44100, 256);
            Host tight(44100, 256);
            const auto yTight = tight.play({ { 100, note, 110 } }, 44100, 256);
            Host off2(44100, 256);
            off2.set(sw.toRawUTF8(), 0.0f);
            auto tailDb = [](const std::vector<float>& x) {
                double e = 0;
                for (size_t i = 8820; i < std::min<size_t>(x.size(), 44100); ++i) e += (double) x[i] * x[i];
                return 10 * std::log10(e + 1e-30);
            };
            const double gain = tailDb(yLoose) - tailDb(yTight);
            check(gain > 3.0, juce::String(key) + ": Wire Tension loose rings longer (tail +" + juce::String(gain, 1) + " dB)");
            check(looseOff.play({ { 100, note, 110 } }, 44100, 256) == off2.play({ { 100, note, 110 } }, 44100, 256),
                  juce::String(key) + ": Wire Tension leaves the snares-off model as it is");
        }
        // other channels are ignored
        Host other(44100, 256);
        const auto y = other.play({ { 100, 38, 127 } }, 4410, 256, 1);
        check(peak(y) < 1e-30, "a note on channel 1 plays nothing");
    }
    {
        // Lug Detune (Kris, 2026-11-27): the toms and snares change, the bass drums don't, 0 is as before
        auto hit = [](const char* param, float v, int note) {
            Host h(44100, 256);
            h.set(param, v);
            return h.play({ { 100, note, 120 } }, 44100, 256);
        };
        const int tomNote = kRows[(size_t) rowIndex("tom3")].defaultNote, kickNote = kRows[(size_t) rowIndex("kick1")].defaultNote;
        const int gongNote = kRows[(size_t) rowIndex("gong")].defaultNote;
        check(hit("tom3_lug_detune", 1.0f, tomNote) != hit("tom3_lug_detune", 0.0f, tomNote)
                  && hit("gong_lug_detune", 1.0f, gongNote) != hit("gong_lug_detune", 0.0f, gongNote),
              "Lug Detune changes Tom 3 and the gong drum");
        check(hit("kick1_lug_detune", 1.0f, kickNote) == hit("kick1_lug_detune", 0.0f, kickNote),
              "Lug Detune leaves Kick 1 as it is (the bass drums don't get it)");
    }
    {
        // the layer blend (Kris, 2026-11-27): the attack's high end moves smoothly with velocity on
        // every drum, through the layers' midpoints and edges (the prototype's way jumped by up to
        // 15 dB there: the octobans' clicks); the plugin turns it on
        Host h(44100, 256);
        const tympanis::KitData* kd = h.p->kitDataForTesting();
        check(kd != nullptr, "the kit's data is loaded");
        if (kd != nullptr)
        {
            // the high end of the first 10 ms (second difference), re the hit's peak, in dB
            auto sweep = [&](int e, bool blend) {
                std::vector<double> att;
                tympanis::Kit kit;
                kit.init(kd, true, false);
                kit.prepare(44100);
                kit.setNoiseless(true);
                kit.drum(e).setLayerBlend(blend);
                std::vector<double> y(13230);
                for (double v = 0.02; v <= 1.0001; v += 0.005)
                {
                    kit.drum(e).reset();
                    kit.strike(e, v, tympanis::Art::Head);
                    std::fill(y.begin(), y.end(), 0.0);
                    kit.render(y.data(), (int) y.size());
                    double pk = 1e-30, hf = 0;
                    for (size_t i = 0; i < y.size(); ++i) pk = std::max(pk, std::abs(y[i]));
                    for (size_t i = 2; i < 441; ++i) hf = std::max(hf, std::abs(y[i] - 2 * y[i - 1] + y[i - 2]));
                    att.push_back(20 * std::log10(hf / pk + 1e-30));
                }
                double worst = 0;
                for (size_t i = 1; i < att.size(); ++i) worst = std::max(worst, std::abs(att[i] - att[i - 1]));
                return worst;
            };
            for (int e = 0; e < (int) kd->drums.size(); ++e)
            {
                const juce::String key = kd->drums[(size_t) e].key;
                if (key == "gong") continue;      // a gentle 5 dB step of its own, not the blend's
                const double off = sweep(e, false), on = sweep(e, true);
                check(on < 3.0, key + ": the attack moves smoothly with velocity (worst step " + juce::String(on, 1)
                                    + " dB; " + juce::String(off, 1) + " dB the prototype's way)");
            }
        }
    }
    {
        // the MIDI map (Kris, 2026-11-26): a channel per drum, notes per channel, map files
        auto drumOf = [](const char* key) {
            for (size_t d = 0; d < kDrums.size(); ++d)
                if (juce::String(kDrums[d].key) == key) return (int) d;
            return -1;
        };
        const int snare = drumOf("snare1"), kick = drumOf("kick1"), tom1 = drumOf("tom1");
        const int snareHead = rowIndex("snare1"), kickHead = rowIndex("kick1"), tom1Head = rowIndex("tom1");
        Host m(44100, 256);
        bool all10 = true;
        for (size_t d = 0; d < kDrums.size(); ++d) all10 = all10 && m.p->getDrumChannel((int) d) == 10;
        check(all10 && m.p->mapName() == "General MIDI" && ! m.p->mapModified() && m.p->mapFile() == juce::File()
                  && m.p->mapToJson() == TympanisProcessor::generalMidiJson(),
              "MIDI map: every drum on channel 10, General MIDI, unchanged");
        // a drum on its own channel plays there only; the others stay on 10
        m.p->setDrumChannel(snare, 2);
        const bool snareOn2 = peak(m.play({ { 100, 38, 110 } }, 4410, 256, 2)) > 0.01;
        Host m10(44100, 256);
        m10.p->setDrumChannel(snare, 2);
        const bool snareOn10 = peak(m10.play({ { 100, 38, 110 } }, 4410, 256, 10)) > 1e-6;
        const bool kickOn10 = peak(m10.play({ { 100, 36, 110 } }, 4410, 256, 10)) > 0.01;
        check(snareOn2 && ! snareOn10 && kickOn10 && m.p->mapModified(),
              "MIDI map: the Snare on channel 2 plays there and not on 10; Kick 1 still on 10; the map shows changed");
        // the same note on two channels: two drums
        Host sh(44100, 256);
        sh.p->setDrumChannel(tom1, 3);
        sh.p->setRowNote(tom1Head, 36);
        check(sh.p->getRowNote(kickHead) == 36 && sh.p->getRowNote(tom1Head) == 36,
              "MIDI map: Tom 1 on channel 3 can share Kick 1's note 36");
        // moving Tom 1 back to 10: it keeps 36, Kick 1's 36 goes to none (the drum moved keeps its notes)
        sh.p->setDrumChannel(tom1, 10);
        check(sh.p->getRowNote(tom1Head) == 36 && sh.p->getRowNote(kickHead) == kNoteNone,
              "MIDI map: moving a drum onto a channel, its notes win a clash there");
        // SETTINGS: one channel for all overrides every drum's own; the first row keeps a clashed note
        Host all(44100, 256);
        all.p->setDrumChannel(tom1, 3);
        all.p->setRowNote(tom1Head, 36);
        all.p->setMidiChannel(5);
        bool all5 = true;
        for (size_t d = 0; d < kDrums.size(); ++d) all5 = all5 && all.p->getDrumChannel((int) d) == 5;
        check(all5 && all.p->getMidiChannel() == 5 && all.p->getRowNote(kickHead) == 36 && all.p->getRowNote(tom1Head) == kNoteNone,
              "MIDI map: setting the channel for all puts every drum on it; the first row keeps a shared note");
        // MIDI learn listens on the learning drum's channel
        Host ln(44100, 256);
        ln.p->setDrumChannel(snare, 2);
        ln.p->startMidiLearn(snareHead);
        ln.play({ { 100, 90, 100 } }, 2048, 256, 10);
        const bool ignored = ln.p->midiLearnRow() == snareHead;
        ln.play({ { 100, 91, 100 } }, 2048, 256, 2);
        check(ignored && ln.p->getRowNote(snareHead) == 91 && ln.p->midiLearnRow() == -1, "MIDI learn: on the drum's own channel");
        // session state: the drums' channels and the map's name
        Host sa(44100, 256);
        sa.p->setDrumChannel(snare, 4);
        sa.p->setRowNote(snareHead, 40);
        juce::MemoryBlock mb;
        sa.p->getStateInformation(mb);
        Host sb(44100, 256);
        sb.p->setStateInformation(mb.getData(), (int) mb.getSize());
        check(sb.p->getDrumChannel(snare) == 4 && sb.p->getDrumChannel(kick) == 10 && sb.p->getRowNote(snareHead) == 40
                  && sb.p->mapName() == "General MIDI" && sb.p->mapModified(),
              "state: each drum's channel and the map restored (General MIDI, changed)");
        // a session from before the drums' own channels: every drum on its saved channel
        auto xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int) mb.getSize());
        if (auto* map = xml != nullptr ? xml->getChildByName("MidiMap") : nullptr)
        {
            for (const auto& d : kDrums) map->removeAttribute(juce::String("ch_") + d.key);
            map->setAttribute("channel", 7);
            for (const char* a : { "name", "file", "base" }) map->removeAttribute(a);
        }
        juce::MemoryBlock old;
        if (xml != nullptr) juce::AudioProcessor::copyXmlToBinary(*xml, old);
        Host sc(44100, 256);
        sc.p->setStateInformation(old.getData(), (int) old.getSize());
        bool all7 = true;
        for (size_t d = 0; d < kDrums.size(); ++d) all7 = all7 && sc.p->getDrumChannel((int) d) == 7;
        check(all7 && sc.p->getMidiChannel() == 7 && sc.p->mapName() == "General MIDI",
              "state: an older session puts every drum on its saved channel");
        // map files: save, change, load back; a file that is not a map changes nothing
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("TympanisHostTestMaps");
        dir.deleteRecursively();
        dir.createDirectory();
        const auto f = dir.getChildFile("Studio B.json");
        Host fm(44100, 256);
        fm.p->setDrumChannel(snare, 2);
        fm.p->setRowNote(kickHead, 33);
        const auto saved = fm.p->mapToJson();
        const bool wrote = fm.p->saveMapFile(f);
        const bool clean = wrote && ! fm.p->mapModified() && fm.p->mapName() == "Studio B" && fm.p->mapFile() == f;
        fm.p->useGeneralMidi();
        const bool gm = fm.p->mapToJson() == TympanisProcessor::generalMidiJson() && fm.p->mapName() == "General MIDI";
        const bool loaded = fm.p->loadMapFile(f);
        check(clean && gm && loaded && fm.p->mapToJson() == saved && fm.p->getDrumChannel(snare) == 2 && fm.p->getRowNote(kickHead) == 33
                  && ! fm.p->mapModified() && fm.p->mapName() == "Studio B",
              "MIDI map files: saved, General MIDI, loaded back the same (named after the file)");
        const auto junk = dir.getChildFile("not a map.json");
        junk.replaceWithText("{ \"hello\": 1 }");
        check(! fm.p->loadMapFile(junk) && fm.p->mapToJson() == saved && fm.p->mapName() == "Studio B",
              "MIDI map files: a file that is not a map is refused and changes nothing");
        dir.deleteRecursively();
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
        check(std::abs(b.p->params.getRawParameterValue("snare1_lug_detune")->load() - 0.75f) < 1e-3f, "state: the Snare's lug detune restored");
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
        h.p->setRowNote(3, 38);                      // the Snare's rim shot takes the Snare's note
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
        check(b.p->getRowNote(rowIndex("tom1")) == 50 && b.p->getRowNote(rowIndex("tom2")) == kNoteNone, "a saved state with a shared note: the first row keeps it");
    }
    {
        // Velocity Sensitivity (as Cymbalis): v = vf + s (v_in - vf), vf = 100/127
        const int note = kRows[2].defaultNote;    // the Snare
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
        const int note = kRows[2].defaultNote;    // the Snare
        Host ref(44100, 256);
        ref.set("snare_buzz", 0.0f);
        const auto y0 = ref.play({ { 10, note, 127 } }, 22050, 256);
        const double p0 = peak(y0);
        check(std::abs(peak(y0) - peak(ref.lastRight)) < 1e-6 * p0, "centre pan: left = right");
        Host off(44100, 256);
        off.set("snare1_level", -60.0f);
        off.set("snare_buzz", 0.0f);                 // else the other snare models buzz along, on their own channels
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
        const int tom3 = kRows[(size_t) rowIndex("tom3")].defaultNote;
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
            check(y0 == y1, "the Snare's Size changes nothing (its depth does not play)");
        }
        check(energy(yd, 22050, 66150) > 2.0 * energy(ya, 22050, 66150), "tom 3 decay 3x: more sound 0.5..1.5 s after the hit");
        check(crossings(yr, 441, 22050) > crossings(yc, 441, 22050),
              "tom 3 strike near the rim is brighter than at the centre (zero crossings " + juce::String(crossings(yr, 441, 22050)) + " vs " + juce::String(crossings(yc, 441, 22050)) + ")");
    }
    {
        // tail: the kit falls silent
        Host h(44100, 512);
        const auto y = h.play({ { 0, 41, 127 }, { 0, 36, 127 }, { 0, 64, 127 } }, 44100 * 30, 512);
        check(peak(y, y.size() - 44100) < 2e-5, "silent 29 s after a hit, below -94 dBFS (" + juce::String(20 * std::log10(peak(y, y.size() - 44100) + 1e-30), 1) + " dBFS)");
    }
    {
        // headroom (Kris, 2026-11-24): no row's full-velocity hit peaks above -3 dBFS at the
        // default settings (4 hits a row, the model varies hit to hit; kRowTrims holds the 4 hot rows)
        double worst = 0;
        juce::String worstRow;
        for (size_t r = 0; r < kRows.size(); ++r)
        {
            Host h(44100, 512);
            std::vector<std::tuple<int64_t, int, int>> ev;
            for (int i = 0; i < 4; ++i) ev.push_back({ (int64_t) i * 44100, kRows[r].defaultNote, 127 });
            const auto y = h.play(ev, 4 * 44100, 512);
            const double p = std::max(peak(y), peak(h.lastRight));
            if (p > worst) { worst = p; worstRow = kRows[r].name; }
        }
        check(worst < std::pow(10.0, -3.0 / 20.0), "headroom: every row's velocity-127 hit peaks below -3 dBFS (loudest "
              + worstRow + ", " + juce::String(20 * std::log10(worst), 1) + " dBFS)");
        // the octobans 4 dB down at Level 0 (Kris, 2026-11-27): Level +4 dB plays them as before
        for (const char* key : { "octo1", "octo2", "octo3", "octo4" })
        {
            double trim = 0;
            for (const auto& t : kRowTrims)
                if (juce::String(t.row) == key) trim = t.db;
            check(std::abs(trim + 4.0) < 1e-9, juce::String(key) + " trimmed 4 dB");
        }
    }
    {
        // the editor's hits: one event per strike, the row and velocity
        Host h(44100, 256);
        h.p->clearHits();
        h.play({ { 10, kRows[(size_t) rowIndex("tom3")].defaultNote, 100 }, { 300, kRows[(size_t) rowIndex("tom3_rim")].defaultNote, 64 }, { 400, 120, 127 } }, 2048, 256);
        TympanisProcessor::HitEvent e {};
        std::vector<TympanisProcessor::HitEvent> got;
        while (h.p->popHit(e)) got.push_back(e);
        check(got.size() == 2 && got[0].row == rowIndex("tom3") && got[1].row == rowIndex("tom3_rim") && std::abs(got[0].velocity - 100.0f / 127.0f) < 1e-3f,
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
        {
            // SETTINGS (page 3) and the shells' finish: saved with the session, not a parameter
            Host c(44100, 256);
            check(c.p->drumFinish() == 0, "the shells' finish starts green");
            const int nParams = c.p->getParameters().size();
            c.p->setEditorPage(3);
            c.p->setDrumFinish(1);
            juce::MemoryBlock mc;
            c.p->getStateInformation(mc);
            Host d(44100, 256);
            d.p->setStateInformation(mc.getData(), (int) mc.getSize());
            check(d.p->editorPage() == 3 && d.p->drumFinish() == 1 && d.p->getParameters().size() == nParams,
                  "state: the SETTINGS page and the natural finish restored (no parameter added)");
            // a session saved before the finish existed: green
            auto xml = juce::AudioProcessor::getXmlFromBinary(mc.getData(), (int) mc.getSize());
            if (auto* ui = xml != nullptr ? xml->getChildByName("Editor") : nullptr) ui->removeAttribute("finish");
            juce::MemoryBlock old;
            if (xml != nullptr) juce::AudioProcessor::copyXmlToBinary(*xml, old);
            b.p->setDrumFinish(1);
            b.p->setStateInformation(old.getData(), (int) old.getSize());
            check(b.p->drumFinish() == 0, "state: a session with no finish saved opens green");
            d.p->setEditorPage(7);
            d.p->setDrumFinish(5);
            check(d.p->editorPage() == 3 && d.p->drumFinish() == 1, "editor page and finish are clamped to 0-3 and 0-1");
            d.p->setEditorPage(3);
            std::unique_ptr<juce::AudioProcessorEditor> de(d.p->createEditor());
            auto* te = dynamic_cast<TympanisEditor*>(de.get());
            using tympanis::ui::Content;
            check(te != nullptr && te->content().wireView(Content::drumPage)->currentFinish() == tympanis::ui::wire::Finish::natural
                      && te->content().wireView(Content::kitPage)->currentFinish() == tympanis::ui::wire::Finish::natural
                      && te->content().wireView(Content::tuningPage)->currentFinish() == tympanis::ui::wire::Finish::natural,
                  "the editor opens with the saved finish on every page");
            te->content().setFinish(0);
            check(d.p->drumFinish() == 0 && te->content().wireView(Content::kitPage)->currentFinish() == tympanis::ui::wire::Finish::green,
                  "choosing a finish on SETTINGS sets it for the session and the drawings");
        }
        {
            // the KIT page: a drum played becomes the selected drum; on the DRUM page it does not
            Host k(44100, 256);
            k.p->setEditorPage(1);
            k.p->setSelectedDrum(8);
            std::unique_ptr<juce::AudioProcessorEditor> ke(k.p->createEditor());
            auto* kt = dynamic_cast<TympanisEditor*>(ke.get());
            const int row = rowIndex("tom1"), tom1 = tympanis::kRows[(size_t) row].drum;
            k.play({ { 10, k.p->getRowNote(row), 100 } }, 2048, 256);
            kt->content().refresh();
            const bool kitSel = k.p->selectedDrum() == tom1;
            kt->content().showPage(tympanis::ui::Content::drumPage);
            const int gongRow = rowIndex("gong");
            k.play({ { 10, k.p->getRowNote(gongRow), 100 } }, 2048, 256);
            kt->content().refresh();
            check(kitSel && k.p->selectedDrum() == tom1, "KIT page: playing a drum selects it; the DRUM page keeps its drum");
        }
        // the editor opens, on the saved page, and closes
        std::unique_ptr<juce::AudioProcessorEditor> ed(b.p->createEditor());
        check(ed != nullptr && ed->getWidth() == 1200 && ed->getHeight() == 800, "the editor opens at the saved size");
        // with no saved size: 1104 x 736, smaller on a small screen (as Cymbalis)
        using tympanis::ui::editorOpeningWidth;
        check(editorOpeningWidth(1920, 1080, 1536) == 1104 && editorOpeningWidth(2560, 1440, 1536) == 1104
                  && editorOpeningWidth(1440, 900, 1536) == 1050 && editorOpeningWidth(1280, 800, 1536) == 960,
              "opening size with no saved size: 1104 wide, smaller on small screens, never below 960 ("
                  + juce::String(editorOpeningWidth(1440, 900, 1536)) + " on 1440 x 900)");
        {
            Host fresh(44100, 256);
            std::unique_ptr<juce::AudioProcessorEditor> fe(fresh.p->createEditor());
            check(fe != nullptr && fe->getWidth() * 2 == fe->getHeight() * 3 && fe->getWidth() <= 1104 && fe->getWidth() >= 960,
                  "a new editor opens at most 1104 x 736, at 3:2: " + juce::String(fe ? fe->getWidth() : 0) + " x " + juce::String(fe ? fe->getHeight() : 0));
        }
        ed.reset();
        check(b.p->getParameters().size() == 124, "the editor adds no parameters (124)");
    }
    {
        // The drums through a pool (test-only thread count): the same bits as one thread, over a busy
        // sequence (every row, groups on one sample, flams a few samples apart, rolls) with the
        // snare buzz up, uneven lug tension and levels and pans moving (the gains ramping), at
        // 64-sample blocks (several runs per block) and 256.
        std::vector<std::tuple<int64_t, int, int>> ev;
        juce::Random rnd(20261125);
        for (int64_t t = 441; t < 44100 * 3; t += 441 + rnd.nextInt(6000))
        {
            const int kind = rnd.nextInt(4);
            const int hits = kind == 0 ? 3 + rnd.nextInt(6) : kind == 1 ? 2 : 1;
            for (int k = 0; k < hits; ++k)
                ev.emplace_back(t + (kind == 1 ? k * (1 + rnd.nextInt(40)) : 0), kRows[(size_t) rnd.nextInt((int) kRows.size())].defaultNote,
                                30 + rnd.nextInt(98));
        }
        std::stable_sort(ev.begin(), ev.end(), [](auto& x, auto& y) { return std::get<0>(x) < std::get<0>(y); });
        for (int blk : { 64, 256 })
        {
            std::vector<float> ref, refR;
            bool same = true;
            juce::String detail;
            for (int threads : { 1, 2, 3 })
            {
                Host h(44100, blk, threads);
                h.set("snare_buzz", 0.9f);
                h.set("tom3_lug_detune", 0.7f);
                h.set("snare1_lug_detune", 0.5f);
                int moves = 0;
                const auto y = h.play(ev, 44100 * 3, blk, 10, [&](int64_t s) {
                    if (s % 8192 < blk)
                    {
                        const char* d = (moves % 3 == 0) ? "snare1" : (moves % 3 == 1) ? "kick1" : "tom2";
                        h.set((juce::String(d) + (moves % 2 ? "_pan" : "_level")).toRawUTF8(), moves % 2 ? (float) ((moves % 5) - 2) * 0.4f : -12.0f + (float) (moves % 7) * 2.0f);
                        ++moves;
                    }
                });
                if (threads == 1) { ref = y; refR = h.lastRight; continue; }
                const bool eq = y == ref && h.lastRight == refR;
                same = same && eq;
                const auto st = h.p->renderPoolForTesting().stats();
                detail += juce::String(threads) + " threads: " + (eq ? "same" : "DIFFERENT") + " (" + juce::String((juce::int64) st.sharedRuns) + " shared runs); ";
            }
            check(same, "drums through a pool: bit-identical to one thread at " + juce::String(blk) + "-sample blocks: " + detail.dropLastCharacters(2));
        }
    }
    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}

// The bench's hits: every 250 ms, alternate halves of the rows (row r when r + k is even, k the
// 250 ms step), velocities 64-127, so all 15 drums keep ringing (the snares' wires-on models:
// the switches at their default).
std::vector<std::tuple<int64_t, int, int>> benchHits(double sr, int64_t n)
{
    std::vector<std::tuple<int64_t, int, int>> ev;
    for (int64_t t = 0, k = 0; t < n; t += (int64_t) (sr * 0.25), ++k)
        for (size_t r = 0; r < kRows.size(); r += 1)
            if ((r + (size_t) k) % 2 == 0) ev.emplace_back(t, kRows[r].defaultNote, 64 + (int) ((r * 17 + (size_t) k) % 64));
    return ev;
}

double quantile(std::vector<float> v, double q)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[(size_t) std::min<double>((double) v.size() - 1, q * (double) (v.size() - 1) + 0.5)];
}

int runBench(double pacedSeconds, bool oneThread)
{
    const auto benchStart = std::chrono::steady_clock::now();
    std::cout << "CPU on one thread: every 250 ms alternate halves of the rows are struck (all 15 drums ringing), snare buzz 0.5,\n"
                 "20 s rendered back to back\n";
    std::cout << "sample rate  block   x real time   % of one core\n";
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int blk : { 64, 256 })
        {
            Host h(sr, blk);
            const int64_t n = (int64_t) (sr * 20);
            const auto ev = benchHits(sr, n);
            const auto t0 = std::chrono::steady_clock::now();
            h.play(ev, n, blk);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("%8.0f   %5d   %10.1f   %11.1f\n", sr, blk, 20.0 / secs, 100.0 * secs / 20.0);
        }
    if (oneThread)
    {
        std::printf("\nbench time: %.0f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - benchStart).count());
        return 0;
    }

    // Per block, paced: the same hits, each block started at its deadline (one block's duration
    // after the last) on a real-time thread, as a host does, and timed. The drums through the
    // pool at 2 and 3 threads (the processor's test-only setting; the plugin uses one).
    std::printf("\nPer block, paced to each block's deadline on a real-time thread (%.0f s after a 1 s lead, same hits)\n", pacedSeconds);
    std::printf("block time as %% of the block: mean, p99, worst, blocks over 100%%; runs: pool runs per block (2 or more drums sounding);\n"
                "caller wait (median / p99 / max, us); wake-up (median / p99, us; %% from sleep); workers' CPU (%% of one core)\n");
    std::printf("  rate block thr |  mean   p99  worst >100%% | runs/blk | caller wait           | wake-up            | workers\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int blk : { 64, 256 })
            for (int threads : { 1, 2, 3 })
            {
                Host h(sr, blk, threads, threads > 1 ? 400000 : 0);
                auto& pool = h.p->renderPoolForTesting();
                const int64_t lead = (int64_t) sr / blk * blk, n = lead + (int64_t) (sr * pacedSeconds) / blk * blk; // whole blocks
                const auto ev = benchHits(sr, n);
                const double periodMs = 1000.0 * blk / sr;
                std::vector<float> pct;
                double cpu0 = 0, cpu1 = 0;
                uint64_t runs0 = 0, runs = 0;
                std::thread t([&] {
                    dhpool::JobPool::makeRealtime(periodMs);
                    juce::AudioBuffer<float> buf(2, blk);
                    size_t ei = 0;
                    auto deadline = std::chrono::steady_clock::now();
                    const auto period = std::chrono::nanoseconds((int64_t) (periodMs * 1.0e6));
                    for (int64_t s = 0; s < n; s += blk)
                    {
                        if (s == lead)
                        {
                            // the lead rendered back to back; then the workers asleep, the counts reset
                            pool.waitIdle(100.0);
                            pool.resetStats();
                            runs0 = pool.stats().runs;
                            cpu0 = pool.workerCpuSeconds();
                            deadline = std::chrono::steady_clock::now();
                        }
                        juce::MidiBuffer midi;
                        while (ei < ev.size() && std::get<0>(ev[ei]) < s + blk)
                        {
                            midi.addEvent(juce::MidiMessage::noteOn(10, std::get<1>(ev[ei]), (juce::uint8) std::get<2>(ev[ei])), (int) (std::get<0>(ev[ei]) - s));
                            ++ei;
                        }
                        if (s >= lead)
                        {
                            std::this_thread::sleep_until(deadline);
                            deadline += period;
                        }
                        const auto a = std::chrono::steady_clock::now();
                        h.p->processBlock(buf, midi);
                        if (s >= lead)
                            pct.push_back((float) (100.0 * std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count() / periodMs));
                    }
                    cpu1 = pool.workerCpuSeconds();
                    runs = pool.stats().runs - runs0;
                });
                t.join();
                double mean = 0;
                int over = 0;
                for (float v : pct) { mean += v; over += v > 100.0f; }
                mean /= std::max<size_t>(1, pct.size());
                const double worst = pct.empty() ? 0.0 : *std::max_element(pct.begin(), pct.end());
                std::printf("%6.1fk %5d %3d | %5.1f %5.1f %6.1f %5d |", sr / 1000, blk, threads, mean, quantile(pct, 0.99), worst, over);
                if (threads == 1)
                {
                    std::printf("        - |                     - |                  - |       -\n");
                    continue;
                }
                const auto st = pool.stats();
                uint64_t wakes = 0, fromSleep = 0;
                for (const auto& w : st.workers) { wakes += w.wakes; fromSleep += w.wakesFromSleep; }
                const auto waits = pool.callerWaitsMicros();
                const auto wakeUs = pool.wakeDelaysMicros();
                const double maxWait = waits.empty() ? 0.0 : *std::max_element(waits.begin(), waits.end());
                std::printf("   %6.2f | %5.1f / %6.1f / %6.0f | %5.1f / %6.1f %3.0f%% | %6.1f\n", (double) runs / std::max<size_t>(1, pct.size()),
                            quantile(waits, 0.5), quantile(waits, 0.99), maxWait, quantile(wakeUs, 0.5), quantile(wakeUs, 0.99),
                            wakes ? 100.0 * (double) fromSleep / (double) wakes : 0.0, 100.0 * (cpu1 - cpu0) / pacedSeconds);
            }
    std::printf("\nbench time: %.0f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - benchStart).count());
    return 0;
}

int runRender(const std::string& seqPath, const std::string& outPath, const std::string& rawPath, int threads)
{
    const auto seq = json::parseFile(seqPath);
    const double sr = seq.at("sample_rate").number();
    const int blk = (int) seq.at("block").number();
    const int64_t n = (int64_t) (seq.at("duration").number() * sr);
    std::map<std::string, int> note;
    for (const auto& r : kRows) note[r.id] = r.defaultNote;
    Host h(sr, blk, threads);
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
    // "automation": [[time_s, parameter id, value], ...], each set before the block it falls in
    std::vector<std::tuple<int64_t, std::string, double>> au;
    if (const auto* a = seq.find("automation"))
        for (const auto& e : a->array())
            au.emplace_back((int64_t) (e.array()[0].number() * sr), e.array()[1].string(), e.array()[2].number());
    std::stable_sort(au.begin(), au.end(), [](auto& x, auto& y) { return std::get<0>(x) < std::get<0>(y); });
    size_t ai = 0;
    const auto y = h.play(ev, n, blk, 10, [&](int64_t s) {
        while (ai < au.size() && std::get<0>(au[ai]) < s + blk)
        {
            if (auto* prm = h.p->params.getParameter(std::get<1>(au[ai])))
                prm->setValueNotifyingHost(prm->convertTo0to1((float) std::get<2>(au[ai])));
            else
                std::cerr << "unknown parameter " << std::get<1>(au[ai]) << "\n";
            ++ai;
        }
    });
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
    // the float output (L then R) as raw samples, for bit-for-bit comparisons (md5sum)
    if (! rawPath.empty())
    {
        std::ofstream f(rawPath, std::ios::binary);
        f.write((const char*) y.data(), (std::streamsize) (y.size() * sizeof(float)));
        f.write((const char*) h.lastRight.data(), (std::streamsize) (h.lastRight.size() * sizeof(float)));
    }
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const std::string mode = argc > 1 ? argv[1] : "checks";
    if (mode == "checks") return runChecks();
    if (mode == "bench")
    {
        double seconds = 4.0;
        bool oneThread = false;
        for (int i = 2; i < argc; ++i)
        {
            if (std::string(argv[i]) == "--seconds" && i + 1 < argc) seconds = std::atof(argv[++i]);
            else if (std::string(argv[i]) == "--one-thread") oneThread = true;
        }
        return runBench(seconds, oneThread);
    }
    if (mode == "render" && argc > 3)
    {
        std::string raw;
        int threads = 1;
        for (int i = 4; i + 1 < argc; i += 2)
        {
            if (std::string(argv[i]) == "--raw") raw = argv[i + 1];
            if (std::string(argv[i]) == "--threads") threads = std::max(1, std::atoi(argv[i + 1]));
        }
        return runRender(argv[2], argv[3], raw, threads);
    }
    std::cerr << "usage: tympanis_hosttest checks | bench | render SEQ.json OUT.wav [--raw OUT.f32] [--threads N]\n";
    return 2;
}
