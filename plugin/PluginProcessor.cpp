#include "PluginProcessor.h"

#include "BinaryData.h"

using namespace tympanis;

namespace
{
constexpr int kStateVersion = 1;

juce::String tuneId(size_t d) { return juce::String(kDrums[d].key) + "_tune"; }
juce::String lugId(size_t d) { return juce::String(kDrums[d].key) + "_lug_detune"; }

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
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (size_t d = 0; d < kDrums.size(); ++d)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(kDrums[d].key, kDrums[d].name, " | ");
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { tuneId(d), 1 }, juce::String(kDrums[d].name) + " Tune",
            juce::NormalisableRange<float>(-2.0f, 2.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("st")));
        group->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { lugId(d), 1 }, juce::String(kDrums[d].name) + " Lug Detune",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::move(group));
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "snare_buzz", 1 }, "Snare Buzz",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    return layout;
}

TympanisProcessor::TympanisProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "TympanisParams", createLayout())
{
    for (size_t d = 0; d < kDrums.size(); ++d)
    {
        tuneParams[d] = params.getRawParameterValue(tuneId(d));
        lugParams[d] = params.getRawParameterValue(lugId(d));
        lastTune[d] = lastLug[d] = std::numeric_limits<float>::quiet_NaN();
    }
    buzzParam = params.getRawParameterValue("snare_buzz");
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
    mono.assign((size_t) std::max(samplesPerBlock, 32), 0.0);
    if (! kitLoaded) return;
    kit.prepare(sampleRate);
    lastBuzz = -1;
    for (size_t d = 0; d < kDrums.size(); ++d) lastTune[d] = lastLug[d] = std::numeric_limits<float>::quiet_NaN();
    applyParameters();
}

void TympanisProcessor::applyParameters()
{
    for (size_t d = 0; d < kDrums.size(); ++d)
    {
        const float t = tuneParams[d]->load(), l = lugParams[d]->load();
        if (! juce::exactlyEqual(t, lastTune[d])) { kit.drum((int) d).setTune(t); lastTune[d] = t; }
        if (! juce::exactlyEqual(l, lastLug[d])) { kit.drum((int) d).setLugDetune(l); lastLug[d] = l; }
    }
    const float b = buzzParam->load();
    if (! juce::exactlyEqual(b, lastBuzz)) { kit.setSnareBuzz(b); lastBuzz = b; }
}

void TympanisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (! kitLoaded || n == 0) return;
    applyParameters();

    const int channel = midiChannel.load();
    const int cap = (int) mono.size();
    auto renderTo = [&](int from, int to) {
        while (from < to)
        {
            const int len = std::min(to - from, cap);
            kit.render(mono.data(), len);
            for (int c = 0; c < buffer.getNumChannels(); ++c)
            {
                float* o = buffer.getWritePointer(c, from);
                for (int i = 0; i < len; ++i) o[i] = (float) (mono[(size_t) i] * kOutputGain);
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
        const double vel = msg.getFloatVelocity();
        for (size_t r = 0; r < kRows.size(); ++r)
            if (rowNotes[r].load() == note) kit.strike(kRows[r].drum, vel, kRows[r].art);   // rows sharing a note all play
    }
    renderTo(pos, n);
}

juce::AudioProcessorEditor* TympanisProcessor::createEditor()
{
    // Placeholder until the UI mockup is approved: the host's generic parameter view.
    return new juce::GenericAudioProcessorEditor(*this);
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
        for (size_t r = 0; r < kRows.size(); ++r)
            setRowNote((int) r, (int) map.getProperty(kRows[r].id, kRows[r].defaultNote));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TympanisProcessor(); }
