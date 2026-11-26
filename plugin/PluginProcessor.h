#pragma once

#include "KitMap.h"
#include "dsp/Engine.h"
#include "JobPool.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class TympanisProcessor : public juce::AudioProcessor
{
public:
    TympanisProcessor();
    ~TympanisProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { renderPool.release(); }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // MIDI map (session state, set from the editor's MIDI CHANNEL box and MIDI NOTE boxes)
    int getMidiChannel() const { return midiChannel.load(); }
    void setMidiChannel(int ch) { midiChannel.store(juce::jlimit(1, 16, ch)); }
    int getRowNote(int row) const { return rowNotes[(size_t) row].load(); }
    // A note belongs to at most one row: giving a row a note another row has sets that row to none.
    void setRowNote(int row, int note);

    // MIDI learn: the next note-on on the MIDI channel goes to this row (and plays it).
    void startMidiLearn(int row) noexcept { learnRow.store(row); }
    void cancelMidiLearn() noexcept { learnRow.store(-1); }
    int midiLearnRow() const noexcept { return learnRow.load(); }

    // Hits for the editor (the lit rows and the moving heads): one event per strike. The audio
    // thread writes it into a preallocated single-producer, single-consumer FIFO; when the FIFO
    // is full, as when no editor reads it, the event is dropped. The editor takes them on its timer.
    struct HitEvent
    {
        std::int8_t row;   // index into kRows
        float velocity;    // 0..1, as the drum plays it (after its Velocity Sensitivity)
    };
    static constexpr int hitFifoSize = 64;
    bool popHit(HitEvent& e) noexcept;
    void clearHits() noexcept; // the editor, when it opens: drops what waited without one

    // Editor state, saved with the session (not host parameters): the page shown (0 DRUM,
    // 1 KIT, 2 TUNING, 3 SETTINGS), the selected drum (KitMap.h index), the shells' finish (0 green,
    // 1 natural maple: a look, not a sound) and the editor's size (0 x 0: never set).
    int editorPage() const noexcept { return uiPage.load(); }
    void setEditorPage(int p) noexcept { uiPage.store(juce::jlimit(0, 3, p)); }
    int drumFinish() const noexcept { return uiFinish.load(); }
    void setDrumFinish(int f) noexcept { uiFinish.store(juce::jlimit(0, 1, f)); }
    int selectedDrum() const noexcept { return uiDrum.load(); }
    void setSelectedDrum(int d) noexcept { uiDrum.store(juce::jlimit(0, (int) tympanis::kDrums.size() - 1, d)); }
    juce::Point<int> editorSize() const noexcept { return { editorWidth.load(), editorHeight.load() }; }
    void setEditorSize(int w, int h) noexcept { editorWidth.store(w); editorHeight.store(h); }

    bool isKitLoaded() const { return kitLoaded; }
    const juce::String& getLoadError() const { return loadError; }

    juce::AudioProcessorValueTreeState params;

    // Fixed output gain under the Main Volume parameter: 0.4 (Kris, 2026-11-24: about Cymbalis's
    // loudness; was 0.2). Full-velocity head hits peak near 1.0 in engine units, about -8 dBFS out
    // at 0 dB level, centre pan, and measure -20 to -30 LUFS momentary (a full Nova Ride bow hit
    // in Cymbalis: -25). The four hottest tom rim shots are trimmed (KitMap.h kRowTrims) so no
    // single hit peaks above -3 dBFS.
    static constexpr double kOutputGain = 0.4;
    // Level span from velocity 0 to 127 (as in every render Kris approved).
    static constexpr double kVelRangeDb = 24.0;
    // Level and Main Volume: -60 dB at the bottom of the range means off.
    static constexpr float kLevelMinDb = -60.0f, kLevelMaxDb = 6.0f;

    // Test only (tools/tympanis_hosttest): render the drums through a pool of `threads` threads,
    // the audio thread included, from the next prepareToPlay; timingSamples > 0 keeps the pool's
    // wake-up delays and caller waits. Not a parameter, not saved with the session, not in the
    // editor: the plugin as shipped renders on one thread (threads 1, no pool). Turning threads
    // on in the plugin is a later step, with Cymbalis's, after tests in hosts.
    void setRenderThreadsForTesting(int threads, int timingSamples = 0) { testThreads = std::max(1, threads); testTimingSamples = timingSamples; }
    dhpool::JobPool& renderPoolForTesting() { return renderPool; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyParameters();

    std::unique_ptr<tympanis::KitData> kitData;
    tympanis::Kit kit;
    dhpool::JobPool renderPool;       // the drums' pool: only with setRenderThreadsForTesting
    int testThreads = 1, testTimingSamples = 0;
    bool kitLoaded = false;
    juce::String loadError;

    struct DrumParams
    {
        std::atomic<float>* tune = nullptr;
        std::atomic<float>* lug = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* strike = nullptr;     // null for the bass drums
        std::atomic<float>* size = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* velSens = nullptr;     // read at each strike
        std::atomic<float>* snares = nullptr;      // the Snares On/Off switch, read at each strike; null without one
        std::atomic<float>* wire = nullptr;        // Snare Wire Tension; null without snares
        float last[6] {};                          // tune, lug, decay, strike, size, wire tension as last applied
    };
    std::array<DrumParams, tympanis::kDrums.size()> drumParams {};
    std::atomic<float>* buzzParam = nullptr;
    std::atomic<float>* mainParam = nullptr;
    float lastBuzz = -1;

    std::atomic<int> midiChannel { tympanis::kDefaultMidiChannel };
    std::array<std::atomic<int>, tympanis::kRows.size()> rowNotes {};
    std::atomic<int> learnRow { -1 };

    void pushHit(int row, float velocity) noexcept;
    juce::AbstractFifo hitFifo { hitFifoSize + 1 }; // room for hitFifoSize events
    std::array<HitEvent, hitFifoSize + 1> hitBuffer {};

    std::atomic<int> uiPage { 0 }, uiDrum { 8 };    // DRUM page, Tom 3 (as the mockups)
    std::atomic<int> uiFinish { 0 };                // green
    std::atomic<int> editorWidth { 0 }, editorHeight { 0 };

    std::vector<double> left, right;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TympanisProcessor)
};
