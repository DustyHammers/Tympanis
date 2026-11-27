#include "PluginProcessor.h"

#include "BinaryData.h"
#include "PackedBake.h"
#include "PluginEditor.h"

using namespace tympanis;

namespace
{
constexpr int kStateVersion = 1;
// Velocity Sensitivity (as Cymbalis): the velocity the drum plays is vf + s (v - vf), v the
// note's velocity / 127, s the drum's Velocity Sensitivity. At 100 % the model's own response;
// lower, the dynamics narrow toward velocity 100; at 0 % every hit plays at velocity 100.
constexpr float kVelocityFloor = 100.0f / 127.0f;
// Lug Detune (Kris, 2026-11-27): at 1 on the toms and snares, 40 % RMS tension error between lugs
// (the engine's 8; its 1 is the prototype's 5 %) with the full character (Drum::setLugCharacter);
// the octobans and the gong drum get half of it; the bass drums none.
constexpr double kLugDetuneScale = 8.0;
double lugShare(const char* key)
{
    const juce::String k(key);
    if (k.startsWith("kick")) return 0.0;
    if (k.startsWith("octo") || k.startsWith("gong")) return 0.5;
    return 1.0;
}
juce::String percentText(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; }

juce::String pid(size_t d, const char* what) { return juce::String(kDrums[d].key) + "_" + what; }

juce::String dbText(float v, int)
{
    return v <= TympanisProcessor::kLevelMinDb + 1e-3f ? juce::String("-inf") : juce::String(v, 1);
}
double dbGain(float v) { return v <= TympanisProcessor::kLevelMinDb + 1e-3f ? 0.0 : std::pow(10.0, v / 20.0); }

juce::NormalisableRange<float> levelRange()
{
    juce::NormalisableRange<float> r(TympanisProcessor::kLevelMinDb, TympanisProcessor::kLevelMaxDb, 0.1f);
    r.setSkewForCentre(-12.0f);
    return r;
}

// The baked kit is embedded packed (resources/bake/*.bake.json -> tools/pack_bake -> BinaryData;
// plugin/PackedBake.h): it unpacks to the bake's JSON tree, bit for bit.
json::Value bakeTree(const juce::String& key)
{
    const juce::String file = key + ".bake.json";
    const juce::String res = packed::resourceName(file).replaceCharacter('.', '_');
    int size = 0;
    const char* data = BinaryData::getNamedResource(res.toRawUTF8(), size);
    if (data == nullptr) throw std::runtime_error(("missing resource " + res).toStdString());
    return packed::unpack(data, (size_t) size, file.toStdString());
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout TympanisProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (size_t d = 0; d < kDrums.size(); ++d)
    {
        const juce::String name(kDrums[d].name);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(kDrums[d].key, name, " | ");
        // Snares On/Off (Kris, 2026-11-26): which of the drum's two models a new hit plays, wires
        // on (default) or off; the Snare and the Popcorn Snare only
        if (kDrums[d].engineOff >= 0)
            group->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { pid(d, "snares"), 1 }, name + " Snares", true,
                juce::AudioParameterBoolAttributes().withStringFromValueFunction([](bool on, int) { return juce::String(on ? "On" : "Off"); })));
        // Snare Wire Tension (Kris, 2026-11-26): 0 tight, as recorded (default) .. 1 loose; the
        // wires-on model only
        if (kDrums[d].engineOff >= 0)
            group->addChild(std::make_unique<P>(juce::ParameterID { pid(d, "wire_tension"), 1 }, name + " Snare Wire Tension",
                juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
        group->addChild(std::make_unique<P>(juce::ParameterID { pid(d, "tune"), 1 }, name + " Tune",
            juce::NormalisableRange<float>(-2.0f, 2.0f, 0.01f), 0.0f, A().withLabel("st")));
        group->addChild(std::make_unique<P>(juce::ParameterID { pid(d, "lug_detune"), 1 }, name + " Lug Detune",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
        juce::NormalisableRange<float> decay(0.25f, 4.0f, 0.01f);
        decay.setSkewForCentre(1.0f);
        group->addChild(std::make_unique<P>(juce::ParameterID { pid(d, "decay"), 1 }, name + " Decay",
            decay, 1.0f, A().withLabel("x")));
        if (kDrums[d].strikePos)
            group->addChild(std::make_unique<P>(juce::ParameterID { pid(d, "strike_pos"), 1 }, name + " Strike Position",
                juce::NormalisableRange<float>(0.0f, 0.95f, 0.005f), 0.35f));
        // Size: the shell's depth (Kris, 2026-11-25), -4 .. +2" in 1" steps; it plays on Toms 1-4
        // only (on the other drums it stays, greyed out in the editor, and changes nothing)
        const float dp = (float) kDrums[d].depthIn;
        group->addChild(std::make_unique<P>(juce::ParameterID { pid(d, "size"), 1 }, name + " Size",
            juce::NormalisableRange<float>(dp - (float) kDepthDownIn, dp + (float) kDepthUpIn, 1.0f), dp, A().withLabel("in")));
        group->addChild(std::make_unique<P>(juce::ParameterID { pid(d, "level"), 1 }, name + " Level",
            levelRange(), 0.0f, A().withLabel("dB").withStringFromValueFunction(dbText)));
        group->addChild(std::make_unique<P>(juce::ParameterID { pid(d, "pan"), 1 }, name + " Pan",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
        group->addChild(std::make_unique<P>(juce::ParameterID { pid(d, "vel_sens"), 1 }, name + " Velocity Sensitivity",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f,
            A().withStringFromValueFunction(percentText)
               .withValueFromStringFunction([](const juce::String& s) { return s.retainCharacters("0123456789.-").getFloatValue() / 100.0f; })));
        layout.add(std::move(group));
    }
    layout.add(std::make_unique<P>(juce::ParameterID { "snare_buzz", 1 }, "Snare Buzz",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    layout.add(std::make_unique<P>(juce::ParameterID { "main_volume", 1 }, "Main Volume",
        levelRange(), 0.0f, A().withLabel("dB").withStringFromValueFunction(dbText)));
    return layout;
}

TympanisProcessor::TympanisProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "TympanisParams", createLayout())
{
    for (size_t d = 0; d < kDrums.size(); ++d)
    {
        auto& p = drumParams[d];
        p.tune = params.getRawParameterValue(pid(d, "tune"));
        p.lug = params.getRawParameterValue(pid(d, "lug_detune"));
        p.decay = params.getRawParameterValue(pid(d, "decay"));
        p.strike = kDrums[d].strikePos ? params.getRawParameterValue(pid(d, "strike_pos")) : nullptr;
        p.size = params.getRawParameterValue(pid(d, "size"));
        p.level = params.getRawParameterValue(pid(d, "level"));
        p.pan = params.getRawParameterValue(pid(d, "pan"));
        p.velSens = params.getRawParameterValue(pid(d, "vel_sens"));
        p.snares = kDrums[d].engineOff >= 0 ? params.getRawParameterValue(pid(d, "snares")) : nullptr;
        p.wire = kDrums[d].engineOff >= 0 ? params.getRawParameterValue(pid(d, "wire_tension")) : nullptr;
    }
    buzzParam = params.getRawParameterValue("snare_buzz");
    mainParam = params.getRawParameterValue("main_volume");
    for (size_t r = 0; r < kRows.size(); ++r) rowNotes[r].store(kRows[r].defaultNote);
    for (auto& c : drumChannels) c.store(kDefaultMidiChannel);
    mapBase = generalMidiJson();

    try
    {
        kitData = std::make_unique<KitData>();
        std::vector<std::string> keys;
        parseKitHeader(bakeTree("kit"), *kitData, keys);
        if (keys.size() != kEngineDrums.size()) throw std::runtime_error("baked kit has the wrong number of drums");
        for (size_t e = 0; e < kEngineDrums.size(); ++e)
        {
            if (keys[e] != kEngineDrums[e].key) throw std::runtime_error("baked kit order differs from KitMap.h");
            kitData->drums.push_back(parseDrum(bakeTree(kEngineDrums[e].key)));
            if (std::abs(kitData->drums.back().nativeIn - kEngineDrums[e].sizeIn) > 1e-9)
                throw std::runtime_error("baked size differs from KitMap.h for " + std::string(kEngineDrums[e].key));
        }
        for (const auto& info : kDrums)
            if (std::string(kEngineDrums[(size_t) info.engine].key) != info.key
                || std::abs(kEngineDrums[(size_t) info.engine].sizeIn - info.sizeIn) > 1e-9)
                throw std::runtime_error("KitMap.h: drum and engine drum differ for " + std::string(info.key));
        kit.init(kitData.get());
        for (int d = 0; d < kit.numDrums(); ++d)
        {
            kit.drum(d).setVelRangeDb(kVelRangeDb);
            kit.drum(d).setLayerBlend(true);    // smooth between velocity layers (Kris, 2026-11-27)
        }
        for (const auto& t : kRowTrims)
            for (const auto& r : kRows)
                if (juce::String(r.id) == t.row)
                    kit.drum(kDrums[(size_t) r.drum].engine).setArtTrim(r.art, std::pow(10.0, t.db / 20.0));
        // Snare Wire Tension: the wires' share of each mode's damping, from the snares-off model
        for (const auto& info : kDrums)
            if (info.engineOff >= 0)
                kit.drum(info.engine).setWireShares(wireShares(kitData->drums[(size_t) info.engine], kitData->drums[(size_t) info.engineOff]));
        kitLoaded = true;
    }
    catch (const std::exception& e)
    {
        loadError = e.what();
        DBG("Tympanis: kit failed to load: " << loadError);
    }
}

bool TympanisProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void TympanisProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    left.assign((size_t) std::max(samplesPerBlock, 32), 0.0);
    right = left;
    if (! kitLoaded) return;
    kit.prepare(sampleRate);
    // one thread unless a test asked for more (setRenderThreadsForTesting)
    if (testThreads > 1)
    {
        dhpool::JobPool::Options o;
        o.threads = testThreads;
        o.periodMs = 1000.0 * samplesPerBlock / sampleRate;
        o.timingSamples = testTimingSamples;
        renderPool.prepare(o);
        kit.setPool(&renderPool);
    }
    else
    {
        kit.setPool(nullptr);
        renderPool.release();
    }
    lastBuzz = -1;
    for (auto& p : drumParams)
        for (float& v : p.last) v = std::numeric_limits<float>::quiet_NaN();
    applyParameters();
    kit.snapGains();     // start at the set levels, no ramp from unity
}

void TympanisProcessor::applyParameters()
{
    const double main = dbGain(mainParam->load()) * kOutputGain;
    for (size_t d = 0; d < kDrums.size(); ++d)
    {
        auto& p = drumParams[d];
        // the drum's engine drums: one, or the wires-on and wires-off models, which share every
        // setting (Kris, 2026-11-26)
        const int engines[2] = { kDrums[d].engine, kDrums[d].engineOff };
        auto each = [&](auto&& f) {
            for (const int e : engines)
                if (e >= 0) f(kit.drum(e));
        };
        auto changed = [&](int i, float v) {
            if (juce::exactlyEqual(v, p.last[i])) return false;
            p.last[i] = v;
            return true;
        };
        if (const float v = p.tune->load(); changed(0, v)) each([&](Drum& dr) { dr.setTune(v); });
        // Lug Detune: the splits and their character together (see kLugDetuneScale)
        if (const float v = p.lug->load(); changed(1, v))
        {
            const double u = v * lugShare(kDrums[d].key);
            each([&](Drum& dr) {
                dr.setLugDetune(kLugDetuneScale * u);
                dr.setLugCharacter(u);
            });
        }
        if (const float v = p.decay->load(); changed(2, v)) each([&](Drum& dr) { dr.setDecay(v); });
        if (p.strike != nullptr)
            if (const float v = p.strike->load(); changed(3, v)) each([&](Drum& dr) { dr.setStrikePos(v); });
        // the head's size is the recorded one (Size no longer scales it); Size is the shell's depth
        if (const float v = p.size->load(); changed(4, v) && kDrums[d].depth) each([&](Drum& dr) { dr.setDepth(kDrums[d].depthIn, v); });
        // Snare Wire Tension: the wires-on model only (the wires-off one has no wires on it)
        if (p.wire != nullptr)
            if (const float v = p.wire->load(); changed(5, v)) kit.drum(kDrums[d].engine).setWireTension(v);
        // level, and pan with the -3 dB compensated constant-power law (0 dB in the centre)
        const double g = dbGain(p.level->load()) * main;
        const double th = juce::MathConstants<double>::pi * 0.25 * (juce::jlimit(-1.0f, 1.0f, p.pan->load()) + 1.0);
        for (const int e : engines)
            if (e >= 0) kit.setDrumGains(e, g * std::sqrt(2.0) * std::cos(th), g * std::sqrt(2.0) * std::sin(th));
    }
    const float b = buzzParam->load();
    if (! juce::exactlyEqual(b, lastBuzz)) { kit.setSnareBuzz(b); lastBuzz = b; }
}

void TympanisProcessor::clearClashes(int keepRow)
{
    const int note = rowNotes[(size_t) keepRow].load();
    if (note < 0) return;
    const int ch = drumChannels[(size_t) kRows[(size_t) keepRow].drum].load();
    for (size_t r = 0; r < kRows.size(); ++r)
        if ((int) r != keepRow && rowNotes[r].load() == note && drumChannels[(size_t) kRows[r].drum].load() == ch)
            rowNotes[r].store(kNoteNone);
}

void TympanisProcessor::setRowNote(int row, int note)
{
    if (note < 0) { rowNotes[(size_t) row].store(kNoteNone); return; }
    rowNotes[(size_t) row].store(juce::jlimit(0, 127, note));
    clearClashes(row);
}

void TympanisProcessor::setMidiChannel(int ch)
{
    ch = juce::jlimit(1, 16, ch);
    midiChannel.store(ch);
    for (auto& c : drumChannels) c.store(ch);
    for (size_t r = 0; r < kRows.size(); ++r) clearClashes((int) r); // in row order: the first keeps a note
}

void TympanisProcessor::setDrumChannel(int drum, int ch)
{
    drumChannels[(size_t) drum].store(juce::jlimit(1, 16, ch));
    for (size_t r = 0; r < kRows.size(); ++r)
        if (kRows[r].drum == drum) clearClashes((int) r);              // the drum moved keeps its notes
}

juce::String TympanisProcessor::mapToJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("format", "Tympanis MIDI map");
    root->setProperty("version", 1);
    root->setProperty("channel", midiChannel.load());
    auto* chans = new juce::DynamicObject();
    for (size_t d = 0; d < kDrums.size(); ++d) chans->setProperty(kDrums[d].key, drumChannels[d].load());
    root->setProperty("drum_channels", juce::var(chans));
    auto* notes = new juce::DynamicObject();
    for (size_t r = 0; r < kRows.size(); ++r)
    {
        const int n = rowNotes[r].load();
        notes->setProperty(kRows[r].id, n < 0 ? juce::var() : juce::var(n));
    }
    root->setProperty("notes", juce::var(notes));
    return juce::JSON::toString(juce::var(root));
}

juce::String TympanisProcessor::generalMidiJson()
{
    auto* root = new juce::DynamicObject();
    root->setProperty("format", "Tympanis MIDI map");
    root->setProperty("version", 1);
    root->setProperty("channel", kDefaultMidiChannel);
    auto* chans = new juce::DynamicObject();
    for (const auto& d : kDrums) chans->setProperty(d.key, kDefaultMidiChannel);
    root->setProperty("drum_channels", juce::var(chans));
    auto* notes = new juce::DynamicObject();
    for (const auto& r : kRows) notes->setProperty(r.id, r.defaultNote < 0 ? juce::var() : juce::var(r.defaultNote));
    root->setProperty("notes", juce::var(notes));
    return juce::JSON::toString(juce::var(root));
}

bool TympanisProcessor::applyMapJson(const juce::String& json)
{
    const auto v = juce::JSON::parse(json);
    if (v.getProperty("format", "").toString() != "Tympanis MIDI map") return false;
    const auto notes = v.getProperty("notes", juce::var()), chans = v.getProperty("drum_channels", juce::var());
    if (! notes.isObject()) return false;
    const int all = juce::jlimit(1, 16, (int) v.getProperty("channel", kDefaultMidiChannel));
    midiChannel.store(all);
    for (size_t d = 0; d < kDrums.size(); ++d)
        drumChannels[d].store(juce::jlimit(1, 16, (int) chans.getProperty(kDrums[d].key, all)));
    for (size_t r = 0; r < kRows.size(); ++r)
    {
        const auto n = notes.getProperty(kRows[r].id, juce::var());
        rowNotes[r].store(n.isVoid() || (int) n < 0 ? kNoteNone : juce::jlimit(0, 127, (int) n));
    }
    for (size_t r = 0; r < kRows.size(); ++r) clearClashes((int) r);
    return true;
}

void TympanisProcessor::useGeneralMidi()
{
    applyMapJson(generalMidiJson());
    mapNameNow = "General MIDI";
    mapFileNow = juce::File();
    mapBase = mapToJson();
}

bool TympanisProcessor::saveMapFile(const juce::File& f)
{
    const auto json = mapToJson();
    if (! f.replaceWithText(json)) return false;
    mapNameNow = f.getFileNameWithoutExtension();
    mapFileNow = f;
    mapBase = json;
    return true;
}

bool TympanisProcessor::loadMapFile(const juce::File& f)
{
    if (! f.existsAsFile() || ! applyMapJson(f.loadFileAsString())) return false;
    mapNameNow = f.getFileNameWithoutExtension();
    mapFileNow = f;
    mapBase = mapToJson();
    return true;
}

juce::String TympanisProcessor::mapName() const { return mapNameNow; }
juce::File TympanisProcessor::mapFile() const { return mapFileNow; }
bool TympanisProcessor::mapModified() const { return mapToJson() != mapBase; }

void TympanisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (! kitLoaded || n == 0) return;
    applyParameters();

    const int cap = (int) left.size();
    const int nch = buffer.getNumChannels();
    auto renderTo = [&](int from, int to) {
        while (from < to)
        {
            const int len = std::min(to - from, cap);
            kit.renderStereo(left.data(), right.data(), len);
            if (nch >= 2)
            {
                float* l = buffer.getWritePointer(0, from);
                float* r = buffer.getWritePointer(1, from);
                for (int i = 0; i < len; ++i) { l[i] = (float) left[(size_t) i]; r[i] = (float) right[(size_t) i]; }
            }
            else if (nch == 1)
            {
                // mono output: (L + R) / 2, so centre-panned drums keep their level
                float* m = buffer.getWritePointer(0, from);
                for (int i = 0; i < len; ++i) m[i] = (float) ((left[(size_t) i] + right[(size_t) i]) * 0.5);
            }
            from += len;
        }
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (! msg.isNoteOn()) continue;                                      // note-offs: drums ring on
        const int channel = msg.getChannel();
        const int at = juce::jlimit(0, n, meta.samplePosition);
        if (at > pos) { renderTo(pos, at); pos = at; }
        const int note = msg.getNoteNumber();
        int learning = learnRow.load();
        if (learning >= 0 && drumChannels[(size_t) kRows[(size_t) learning].drum].load() == channel
            && learnRow.compare_exchange_strong(learning, -1))
            setRowNote(learning, note);                                       // then it plays
        for (size_t r = 0; r < kRows.size(); ++r)
            if (rowNotes[r].load() == note && drumChannels[(size_t) kRows[r].drum].load() == channel)
            {
                const auto& dp = drumParams[(size_t) kRows[r].drum];
                const auto& info = kDrums[(size_t) kRows[r].drum];
                const float s = dp.velSens->load();
                const float v = kVelocityFloor + s * (msg.getFloatVelocity() - kVelocityFloor);
                // the Snares switch picks the model a new hit plays; what rings keeps ringing
                const bool off = dp.snares != nullptr && dp.snares->load() < 0.5f;
                kit.strike(off ? info.engineOff : info.engine, v, kRows[r].art);
                pushHit((int) r, v);
                break;                                                        // a note on a channel has at most one row
            }
    }
    renderTo(pos, n);
}

void TympanisProcessor::pushHit(int row, float velocity) noexcept
{
    const auto scope = hitFifo.write(1);
    if (scope.blockSize1 > 0)
        hitBuffer[(size_t) scope.startIndex1] = { (std::int8_t) row, velocity };
}

bool TympanisProcessor::popHit(HitEvent& e) noexcept
{
    const auto scope = hitFifo.read(1);
    if (scope.blockSize1 <= 0) return false;
    e = hitBuffer[(size_t) scope.startIndex1];
    return true;
}

void TympanisProcessor::clearHits() noexcept
{
    HitEvent e;
    while (popHit(e)) {}
}

juce::AudioProcessorEditor* TympanisProcessor::createEditor()
{
    return new TympanisEditor(*this);
}

void TympanisProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    juce::ValueTree root("TympanisState");
    root.setProperty("version", kStateVersion, nullptr);
    root.appendChild(state, nullptr);
    juce::ValueTree map("MidiMap");
    map.setProperty("channel", midiChannel.load(), nullptr);
    for (size_t r = 0; r < kRows.size(); ++r) map.setProperty(kRows[r].id, rowNotes[r].load(), nullptr);
    for (size_t d = 0; d < kDrums.size(); ++d) map.setProperty(juce::String("ch_") + kDrums[d].key, drumChannels[d].load(), nullptr);
    map.setProperty("name", mapNameNow, nullptr);
    map.setProperty("file", mapFileNow.getFullPathName(), nullptr);
    map.setProperty("base", mapBase, nullptr);
    root.appendChild(map, nullptr);
    juce::ValueTree ui("Editor");
    ui.setProperty("page", uiPage.load(), nullptr);
    ui.setProperty("drum", kDrums[(size_t) uiDrum.load()].key, nullptr);
    ui.setProperty("finish", uiFinish.load() == 1 ? "natural" : "green", nullptr);
    ui.setProperty("width", editorWidth.load(), nullptr);
    ui.setProperty("height", editorHeight.load(), nullptr);
    root.appendChild(ui, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary(*xml, destData);
}

void TympanisProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) return;
    const auto root = juce::ValueTree::fromXml(*xml);
    if (! root.hasType("TympanisState")) return;
    const auto state = root.getChildWithName(params.state.getType());
    if (state.isValid()) params.replaceState(state);
    const auto map = root.getChildWithName("MidiMap");
    if (map.isValid())
    {
        // a session from before the drums' own channels: every drum on the saved channel
        const int all = juce::jlimit(1, 16, (int) map.getProperty("channel", kDefaultMidiChannel));
        midiChannel.store(all);
        for (size_t d = 0; d < kDrums.size(); ++d)
            drumChannels[d].store(juce::jlimit(1, 16, (int) map.getProperty(juce::String("ch_") + kDrums[d].key, all)));
        for (size_t r = 0; r < kRows.size(); ++r)
        {
            const int note = (int) map.getProperty(kRows[r].id, kRows[r].defaultNote);
            rowNotes[r].store(note < 0 ? kNoteNone : juce::jlimit(0, 127, note));
        }
        // the first row (in row order) keeps a note that several saved rows share on a channel
        for (size_t r = 0; r < kRows.size(); ++r) clearClashes((int) r);
        mapNameNow = map.getProperty("name", "General MIDI").toString();
        const juce::String path = map.getProperty("file", "").toString();
        mapFileNow = path.isNotEmpty() && juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
        mapBase = map.hasProperty("base") ? map.getProperty("base").toString() : generalMidiJson();
    }
    const auto ui = root.getChildWithName("Editor");
    if (ui.isValid())
    {
        setEditorPage((int) ui.getProperty("page", 0));
        const juce::String key = ui.getProperty("drum", "").toString();
        for (size_t d = 0; d < kDrums.size(); ++d)
            if (key == kDrums[d].key) setSelectedDrum((int) d);
        setDrumFinish(ui.getProperty("finish", "green").toString() == "natural" ? 1 : 0);
        const int w = (int) ui.getProperty("width", 0), h = (int) ui.getProperty("height", 0);
        if (w > 0 && h > 0) setEditorSize(w, h);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TympanisProcessor(); }
