#pragma once

#include "KitMap.h"
#include "dsp/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

class TympanisProcessor : public juce::AudioProcessor
{
public:
    TympanisProcessor();
    ~TympanisProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
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

    // MIDI map (session state; the note-row UI will use these once its mockup is approved)
    int getMidiChannel() const { return midiChannel.load(); }
    void setMidiChannel(int ch) { midiChannel.store(juce::jlimit(1, 16, ch)); }
    int getRowNote(int row) const { return rowNotes[(size_t) row].load(); }
    void setRowNote(int row, int note) { rowNotes[(size_t) row].store(note < 0 ? tympanis::kNoteNone : juce::jlimit(0, 127, note)); }

    bool isKitLoaded() const { return kitLoaded; }
    const juce::String& getLoadError() const { return loadError; }

    juce::AudioProcessorValueTreeState params;

    // Fixed output gain (not a parameter). Full-velocity head hits peak near 1.0 in engine units
    // (about -14 dBFS out); the loudest single hit, tom 3's rim shot (3.8), peaks at about -2.3 dBFS.
    static constexpr double kOutputGain = 0.2;
    // Level span from velocity 0 to 127 (as in every render Kris approved).
    static constexpr double kVelRangeDb = 24.0;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyParameters();

    std::unique_ptr<tympanis::KitData> kitData;
    tympanis::Kit kit;
    bool kitLoaded = false;
    juce::String loadError;

    std::array<std::atomic<float>*, tympanis::kDrums.size()> tuneParams {}, lugParams {};
    std::atomic<float>* buzzParam = nullptr;
    std::array<float, tympanis::kDrums.size()> lastTune {}, lastLug {};
    float lastBuzz = -1;

    std::atomic<int> midiChannel { tympanis::kDefaultMidiChannel };
    std::array<std::atomic<int>, tympanis::kRows.size()> rowNotes {};

    std::vector<double> mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TympanisProcessor)
};
