// tympanis_snapshot — renders the editor's pages to PNG files (docs/UI.md).
//
//   tympanis_snapshot OUT_DIR
//
// Writes, at 1536 x 1024 unless said:
//   drum-tom3.png           DRUM page, Tom 3 at rest
//   drum-tom3-960.png       the same at the smallest size, 960 x 640
//   drum-snare-hit.png      DRUM page, the Snare 50 ms after a full-velocity head hit
//   drum-<key>.png          DRUM page, every other drum at rest
//   kit-tom3.png            KIT page, Tom 3 selected
//   kit-snare1-hit.png      KIT page, Tom 3 selected, the Snare 50 ms after a hit
//   kit-pedals.png          KIT page at rest: the bass drum pedals
//   kit-kick-hits.png       KIT page: Kick 1's pedal at its hit, Kick 2's 60 ms after its hit
//   tuning-tom3.png         TUNING page, Tom 3
//   tuning-kick1.png        TUNING page, Kick 1 (no Strike Position)
//   tuning-snare1.png       TUNING page, the Snare: its SNARES switch on (Size greyed out)
//   tuning-snare1-off.png   the same with the switch off
//   tuning-snare4.png       TUNING page, the Popcorn Snare
//   kit-toms-shallow.png    KIT page, Toms 1-4 at their shallowest (Size -4"): lower on the kicks
//   drum-tom1-shallow.png   DRUM page, Tom 1 at 6" deep
//   tuning-tom1-shallow.png TUNING page, the same
//   drum-snare-hit-2x.png   the DRUM page's hit at twice the pixels (a Retina display)
// and prints how long the wireframes took to draw: the still drawing, and one moving frame.
//
// The plugin is built without JUCE's splash logo (JUCE_DISPLAY_SPLASH_SCREEN=0).
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstdio>

namespace
{
int failures = 0;

void save (juce::AudioProcessorEditor& editor, const juce::File& file)
{
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    if (! out.openedOk() || ! png.writeImageToStream (image, out))
    {
        std::fprintf (stderr, "cannot write %s\n", file.getFullPathName().toRawUTF8());
        ++failures;
        return;
    }
    std::printf ("%s (%d x %d)\n", file.getFileName().toRawUTF8(), image.getWidth(), image.getHeight());
}

void save2x (juce::AudioProcessorEditor& editor, const juce::File& file)
{
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    if (! out.openedOk() || ! png.writeImageToStream (image, out))
        ++failures;
    else
        std::printf ("%s (%d x %d)\n", file.getFileName().toRawUTF8(), image.getWidth(), image.getHeight());
}

int drumOf (const char* key)
{
    for (size_t d = 0; d < tympanis::kDrums.size(); ++d)
        if (juce::String (tympanis::kDrums[d].key) == key)
            return (int) d;
    return 0;
}

int rowOf (const char* id)
{
    for (size_t r = 0; r < tympanis::kRows.size(); ++r)
        if (juce::String (tympanis::kRows[r].id) == id)
            return (int) r;
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: tympanis_snapshot OUT_DIR\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    dir.createDirectory();

    TympanisProcessor processor;
    if (! processor.isKitLoaded())
        std::fprintf (stderr, "warning: kit failed to load: %s\n", processor.getLoadError().toRawUTF8());

    using Content = tympanis::ui::Content;
    auto open = [&processor] (int page, int drum, int width = 1536) {
        processor.setEditorPage (page);
        processor.setSelectedDrum (drum);
        std::unique_ptr<TympanisEditor> e (dynamic_cast<TympanisEditor*> (processor.createEditor()));
        e->setSize (width, width * 2 / 3);
        return e;
    };
    auto timing = [] (const char* what, tympanis::ui::WireView* v) {
        std::printf ("  %s: still %.1f ms, moving frame %.1f ms\n", what, v->lastStillMs(), v->lastFrameMs());
    };

    {
        auto e = open (Content::drumPage, drumOf ("tom3"));
        save (*e, dir.getChildFile ("drum-tom3.png"));
        timing ("DRUM page", e->content().wireView (Content::drumPage));
    }
    {
        auto e = open (Content::drumPage, drumOf ("tom3"), 960);
        save (*e, dir.getChildFile ("drum-tom3-960.png"));
    }
    {
        auto e = open (Content::drumPage, drumOf ("snare1"));
        save (*e, dir.getChildFile ("drum-snare1.png")); // builds the drawing
        e->content().showHit (rowOf ("snare1"), 1.0f, 0.05);
        save (*e, dir.getChildFile ("drum-snare-hit.png"));
        timing ("DRUM page, hit", e->content().wireView (Content::drumPage));
    }
    for (int d = 0; d < (int) tympanis::kDrums.size(); ++d)
    {
        if (d == drumOf ("snare1") || d == drumOf ("tom3"))
            continue;
        auto e = open (Content::drumPage, d);
        save (*e, dir.getChildFile (juce::String ("drum-") + tympanis::kDrums[(size_t) d].key + ".png"));
    }
    {
        auto e = open (Content::kitPage, drumOf ("tom3"));
        save (*e, dir.getChildFile ("kit-tom3.png"));
        timing ("KIT page", e->content().wireView (Content::kitPage));
        e->content().showHit (rowOf ("snare1"), 0.9f, 0.05);
        save (*e, dir.getChildFile ("kit-snare1-hit.png"));
        timing ("KIT page, hit", e->content().wireView (Content::kitPage));
    }
    {
        // a kick's pedal: against the head at the hit, on its way back 60 ms later
        auto e = open (Content::kitPage, drumOf ("tom3"));
        save (*e, dir.getChildFile ("kit-pedals.png"));
        e->content().showHit (rowOf ("kick1"), 1.0f, 0.0);
        e->content().showHit (rowOf ("kick2"), 1.0f, 0.06);
        save (*e, dir.getChildFile ("kit-kick-hits.png"));
        timing ("KIT page, kick hits", e->content().wireView (Content::kitPage));
    }
    {
        // a Retina display: twice the pixels (timings only)
        auto e = open (Content::drumPage, drumOf ("snare1"));
        e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f);
        timing ("DRUM page at 2x", e->content().wireView (Content::drumPage));
        e->content().showHit (rowOf ("snare1"), 1.0f, 0.05);
        save2x (*e, dir.getChildFile ("drum-snare-hit-2x.png"));
        timing ("DRUM page at 2x, hit", e->content().wireView (Content::drumPage));
    }
    {
        auto e = open (Content::kitPage, drumOf ("tom3"));
        e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f);
        timing ("KIT page at 2x", e->content().wireView (Content::kitPage));
        e->content().selectDrum (drumOf ("tom5"));
        timing ("KIT page at 2x, another drum selected", e->content().wireView (Content::kitPage));
        e->content().showHit (rowOf ("tom5"), 1.0f, 0.05);
        timing ("KIT page at 2x, hit", e->content().wireView (Content::kitPage));
    }
    {
        auto e = open (Content::tuningPage, drumOf ("tom3"));
        save (*e, dir.getChildFile ("tuning-tom3.png"));
        timing ("TUNING page", e->content().wireView (Content::tuningPage));
    }
    {
        auto e = open (Content::tuningPage, 0);
        save (*e, dir.getChildFile ("tuning-kick1.png"));
    }
    {
        auto e = open (Content::tuningPage, drumOf ("snare1"));
        save (*e, dir.getChildFile ("tuning-snare1.png"));
    }
    {
        auto* sw = processor.params.getParameter ("snare1_snares");
        sw->setValueNotifyingHost (0.0f);
        auto e = open (Content::tuningPage, drumOf ("snare1"));
        save (*e, dir.getChildFile ("tuning-snare1-off.png"));
        sw->setValueNotifyingHost (sw->getDefaultValue());
    }
    {
        auto e = open (Content::tuningPage, drumOf ("snare4"));
        save (*e, dir.getChildFile ("tuning-snare4.png"));
    }
    {
        // Size (the shell's depth) on Toms 1-4 at its lowest: the drawings follow it
        for (int d = drumOf ("tom1"); d <= drumOf ("tom4"); ++d)
            if (auto* p = processor.params.getParameter (juce::String (tympanis::kDrums[(size_t) d].key) + "_size"))
                p->setValueNotifyingHost (0.0f);
        const std::pair<int, const char*> shots[] = { { Content::kitPage, "kit-toms-shallow.png" }, { Content::drumPage, "drum-tom1-shallow.png" },
                                                       { Content::tuningPage, "tuning-tom1-shallow.png" } };
        for (const auto& [page, name] : shots)
        {
            auto e = open (page, page == Content::kitPage ? drumOf ("tom3") : drumOf ("tom1"));
            e->content().refresh();
            save (*e, dir.getChildFile (name));
        }
        for (int d = drumOf ("tom1"); d <= drumOf ("tom4"); ++d)
            if (auto* p = processor.params.getParameter (juce::String (tympanis::kDrums[(size_t) d].key) + "_size"))
                p->setValueNotifyingHost (p->getDefaultValue());
    }
    return failures == 0 ? 0 : 1;
}
