#include "PluginProcessor.h"

#include "BinaryData.h"
#include "PluginEditor.h"

using namespace tympanis;

namespace
{
constexpr int kStateVersion = 1;
// Velocity Sensitivity (as Cymbalis): the velocity the drum plays is vf + s (v - vf), v the
// note's velocity / 127, s the drum's Velocity Sensitivity. At 100 % the model's own response;
// lower, the dynamics narrow toward velocity 100; at 0 % every hit plays at velocity 100.
constexpr float kVelocityFloor = 100.0f / 127.0f;
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

// The baked kit is embedded gzipped (resources/bake/*.bake.json -> BinaryData).
std::string bakeText(const juce::String& key)
{
    const juce::String res = key + "_bake_json_gz";
    int size = 0;
    const char* data = BinaryData::getNamedResource(res.toRawUTF8(), size);
    if (data == nullptr) throw std::runtime_error(("missing resource " + res).toStdString());
    juce::MemoryInputStream raw(data, (size_t) size, false);
    juce::GZIPDecompressorInputStream gz(&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock mb;
    juce::MemoryOutputStream out(mb, false);
    out.writeFromInputStream(gz, -1);
    return std::string((const char*) out.getData(), out.getDataSize());
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
        const float sz = (float) kDrums[d].sizeIn;
        juce::NormalisableRange<float> size(0.5f * sz, 2.0f * sz, 0.1f);
        size.setSkewForCentre(sz);
        group->addChild(std::make_unique<P>(juce::ParameterID { pid(d, "size"), 1 }, name + " Size",
            size, sz, A().withLabel("in")));
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
    }
    buzzParam = params.getRawParameterValue("snare_buzz");
    mainParam = params.getRawParameterValue("main_volume");
    for (size_t r = 0; r < kRows.size(); ++r) rowNotes[r].store(kRows[r].defaultNote);

    try
    {
        kitData = std::make_unique<KitData>();
        std::vector<std::string> keys;
        parseKitHeader(bakeText("kit"), *kitData, keys);
        if (keys.size() != kDrums.size()) throw std::runtime_error("baked kit has the wrong number of drums");
        for (size_t d = 0; d < kDrums.size(); ++d)
        {
            if (keys[d] != kDrums[d].key) throw std::runtime_error("baked kit order differs from KitMap.h");
            kitData->drums.push_back(parseDrum(bakeText(kDrums[d].key)));
            if (std::abs(kitData->drums.back().nativeIn - kDrums[d].sizeIn) > 1e-9)
                throw std::runtime_error("baked size differs from KitMap.h for " + std::string(kDrums[d].key));
        }
        kit.init(kitData.get());
        for (int d = 0; d < kit.numDrums(); ++d) kit.drum(d).setVelRangeDb(kVelRangeDb);
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
        Drum& dr = kit.drum((int) d);
        auto changed = [&](int i, float v) {
            if (juce::exactlyEqual(v, p.last[i])) return false;
            p.last[i] = v;
            return true;
        };
        if (const float v = p.tune->load(); changed(0, v)) dr.setTune(v);
        if (const float v = p.lug->load(); changed(1, v)) dr.setLugDetune(v);
        if (const float v = p.decay->load(); changed(2, v)) dr.setDecay(v);
        if (p.strike != nullptr)
            if (const float v = p.strike->load(); changed(3, v)) dr.setStrikePos(v);
        if (const float v = p.size->load(); changed(4, v)) dr.setSize(v);
        // level, and pan with the -3 dB compensated constant-power law (0 dB in the centre)
        const double g = dbGain(p.level->load()) * main;
        const double th = juce::MathConstants<double>::pi * 0.25 * (juce::jlimit(-1.0f, 1.0f, p.pan->load()) + 1.0);
        kit.setDrumGains((int) d, g * std::sqrt(2.0) * std::cos(th), g * std::sqrt(2.0) * std::sin(th));
    }
    const float b = buzzParam->load();
    if (! juce::exactlyEqual(b, lastBuzz)) { kit.setSnareBuzz(b); lastBuzz = b; }
}

void TympanisProcessor::setRowNote(int row, int note)
{
    if (note < 0) { rowNotes[(size_t) row].store(kNoteNone); return; }
    note = juce::jlimit(0, 127, note);
    for (size_t r = 0; r < kRows.size(); ++r)
        if ((int) r != row && rowNotes[r].load() == note) rowNotes[r].store(kNoteNone);
    rowNotes[(size_t) row].store(note);
}

void TympanisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (! kitLoaded || n == 0) return;
    applyParameters();

    const int channel = midiChannel.load();
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
        if (! msg.isNoteOn() || msg.getChannel() != channel) continue;      // note-offs: drums ring on
        const int at = juce::jlimit(0, n, meta.samplePosition);
        if (at > pos) { renderTo(pos, at); pos = at; }
        const int note = msg.getNoteNumber();
        int learning = learnRow.load();
        if (learning >= 0 && learnRow.compare_exchange_strong(learning, -1))
            setRowNote(learning, note);                                       // then it plays
        for (size_t r = 0; r < kRows.size(); ++r)
            if (rowNotes[r].load() == note)
            {
                const float s = drumParams[(size_t) kRows[r].drum].velSens->load();
                const float v = kVelocityFloor + s * (msg.getFloatVelocity() - kVelocityFloor);
                kit.strike(kRows[r].drum, v, kRows[r].art);
                pushHit((int) r, v);
                break;                                                        // a note has at most one row
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
    root.appendChild(map, nullptr);
    juce::ValueTree ui("Editor");
    ui.setProperty("page", uiPage.load(), nullptr);
    ui.setProperty("drum", kDrums[(size_t) uiDrum.load()].key, nullptr);
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
        setMidiChannel((int) map.getProperty("channel", kDefaultMidiChannel));
        // the first row (in row order) keeps a note that several saved rows share
        std::array<bool, 128> used {};
        for (size_t r = 0; r < kRows.size(); ++r)
        {
            int note = (int) map.getProperty(kRows[r].id, kRows[r].defaultNote);
            note = note < 0 ? kNoteNone : juce::jlimit(0, 127, note);
            if (note >= 0 && used[(size_t) note]) note = kNoteNone;
            if (note >= 0) used[(size_t) note] = true;
            rowNotes[r].store(note);
        }
    }
    const auto ui = root.getChildWithName("Editor");
    if (ui.isValid())
    {
        setEditorPage((int) ui.getProperty("page", 0));
        const juce::String key = ui.getProperty("drum", "").toString();
        for (size_t d = 0; d < kDrums.size(); ++d)
            if (key == kDrums[d].key) setSelectedDrum((int) d);
        const int w = (int) ui.getProperty("width", 0), h = (int) ui.getProperty("height", 0);
        if (w > 0 && h > 0) setEditorSize(w, h);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TympanisProcessor(); }
