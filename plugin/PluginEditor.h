#pragma once

// The editor (docs/UI.md): three pages in Cymbalis's look, 1536 x 1024 design units scaled with
// one transform. DRUM: the selected drum's wireframe, its note rows, Velocity Sensitivity,
// Level, Pan and Strike Position, and Main Volume. KIT: the kit's wireframe; a click on a drum selects it. TUNING: the
// selected drum's parameters as sliders, and the whole kit's.

#include "ui/EditorSize.h"
#include "PluginProcessor.h"
#include "ui/UiAssets.h"
#include "ui/WireView.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace tympanis::ui
{

class Knob;
class DropdownBox;
class NoteBox;
class TabButton;
class TuneRow;
class FooterButton;
class FinishButton;
class PresetBox;

class Content final : public juce::Component, private juce::Slider::Listener
{
public:
    explicit Content (TympanisProcessor& p);
    ~Content() override;

    enum Page
    {
        drumPage = 0,
        kitPage = 1,
        tuningPage = 2,
        settingsPage = 3
    };
    void showPage (int page);
    void selectDrum (int drum);
    void setFinish (int finish); // 0 green, 1 natural maple

    // Called by the editor's timer: hits, notes learnt on the audio thread, the LEARN pulse.
    void refresh();

    // For tools/tympanis_snapshot: a hit on a row as if it had just played (ageSeconds ago), and
    // the page's wireframe view (SETTINGS: the green one's).
    void showHit (int row, float velocity, double ageSeconds = 0.0);
    WireView* wireView (int page) const;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    struct KnobSpec
    {
        const char* suffix; // the drum's parameter (<drum key>_<suffix>), or a whole parameter ID
        bool perDrum;
        juce::Point<float> centre;
        const char* label;
        const char* leftEnd;
        const char* rightEnd;
        bool bipolar;
    };

    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void sliderDragEnded (juce::Slider*) override;

    void attachDrum();
    void layoutTuningRows();
    juce::Rectangle<int> knobLabelArea (int knob) const;
    void showPopup (std::unique_ptr<juce::Component> panel, juce::Rectangle<int> bounds, std::function<void()> onClose);
    void closePopup();
    void openDrumMenu();
    void openChannelMenu();
    void openNoteMenu (int box);
public:
    void openGridMenu (int row); // the SETTINGS grid's note menu for a row (public for the snapshot tool)
    void openPresetMenuForTest() { openPresetMenu(); }
private:
    void resetParams (bool wholeKit);
    bool drumModified() const;
    juce::String paramId (const char* suffix) const;

    void paintCommon (juce::Graphics& g);
    void paintDrumPage (juce::Graphics& g);
    void paintKitPage (juce::Graphics& g);
    void paintTuningPage (juce::Graphics& g);
    void paintSettingsPage (juce::Graphics& g);
    juce::Rectangle<int> rowLabelArea (int box) const;

    TympanisProcessor& processor;
    Assets assets;
    int page = kitPage;
    int drum = 8;

    std::unique_ptr<WireView> drumView, kitView, smallView;
    std::array<std::unique_ptr<TabButton>, 4> tabs;
    std::unique_ptr<DropdownBox> drumBox, channelBox;
    juce::Path logoPath;

    // DRUM page
    std::array<KnobSpec, 9> knobSpecs;
    std::array<std::unique_ptr<Knob>, 9> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 9> knobAttachments;
    int draggingKnob = -1;
    static constexpr int kMaxRows = 3;
    std::array<std::unique_ptr<NoteBox>, kMaxRows> noteBoxes;
    std::vector<int> drumRows; // the selected drum's rows (kRows indices), shown in this order
    std::array<int, kMaxRows> shownNotes {};

    // SETTINGS page: the two finishes, each a box with Tom 3 drawn in it; a click chooses
    int finish = 0;
    std::array<std::unique_ptr<FinishButton>, 2> finishButtons;
    std::array<std::unique_ptr<WireView>, 2> finishViews;
    // SETTINGS page: the MIDI MAP: a note box per row in a grid (drums down, articulations
    // across), the PRESET box and SAVE AS... (Kris, 2026-11-26, mockups approved)
    std::vector<std::unique_ptr<NoteBox>> gridBoxes;
    std::vector<int> shownGridNotes;
    std::unique_ptr<PresetBox> presetBox;
    std::unique_ptr<FooterButton> saveAsButton;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String shownMapText, shownOwnChannels;
    static int gridRowY (int drum);
    static int gridCellX (int column);
    static juce::Rectangle<int> gridCell (int row);
    void openPresetMenu();
    void saveMapAs();
    void loadMap();
    void loadMapFile (const juce::File& f);
    juce::String ownChannelsText() const; // the drums on their own channel, for the MIDI MAP band

    // TUNING page
    std::vector<std::unique_ptr<TuneRow>> drumTuneRows;
    std::unique_ptr<TuneRow> snaresRow, wireRow; // the Snares switch and Wire Tension (snares only)
    std::array<std::unique_ptr<TuneRow>, 2> kitTuneRows;
    std::unique_ptr<FooterButton> resetDrumButton, resetAllButton;
    bool shownModified = false;

    // Popup: a transparent layer that closes it on any click outside, and the panel.
    std::unique_ptr<juce::Component> popupLayer;
    std::unique_ptr<juce::Component> popup;
    std::function<void()> popupClosed;

    // Hits: each row lit until its hit's flash ends (0.25 + 0.25 v seconds).
    std::array<double, kRows.size()> litUntilMs {};
    std::array<bool, kRows.size()> lit {};
    int shownChannel = -1;
    std::array<double, tympanis::kDrums.size()> shownDepthOff {}; // Size on Toms 1-4, inches off the drawn depth
    std::array<bool, tympanis::kDrums.size()> shownSnaresOff {};  // the SNARES switches off (all on to start)
    int kitHitDrum = -1; // the KIT page: the drum last played since the last refresh()
    int shownLearn = -1;
    double learnStartMs = 0.0;
};

} // namespace tympanis::ui

class TympanisEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TympanisEditor (TympanisProcessor&);
    ~TympanisEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    tympanis::ui::Content& content() noexcept { return *contentComponent; }

private:
    void timerCallback() override;

    class ResizerLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    };

    TympanisProcessor& processorRef;
    ResizerLookAndFeel resizerLookAndFeel;
    bool sizeIsSet = false;
    std::unique_ptr<tympanis::ui::Content> contentComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TympanisEditor)
};
