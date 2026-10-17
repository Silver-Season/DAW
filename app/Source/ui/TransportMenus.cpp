#include "TransportMenus.h"

#include "../Strings.h"
#include "../model/ProjectModel.h"

#include <cmath>

namespace ui
{
juce::String formatPosition (double beats, double beatsPerBar)
{
    const double bpb = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    if (beats < -1.0e-9)
    {
        const int bar = (int) std::floor (beats / bpb + 1.0e-9);
        double inBar = std::fmod (beats - bar * bpb, bpb);
        if (inBar < 0.0)
            inBar += bpb;
        const int beat = juce::jlimit (1, (int) std::ceil (bpb), (int) std::floor (inBar + 1.0e-9) + 1);
        return juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (-bar) + ". " + juce::String (beat);
    }
    const int bar = (int) std::floor (beats / bpb + 1.0e-9) + 1;
    const int beat = (int) std::floor (std::fmod (beats, bpb) + 1.0e-9) + 1;
    const int sixteenth = (int) std::floor (std::fmod (beats * 4.0, 4.0)) + 1;
    return juce::String (bar) + ". " + juce::String (beat) + ". " + juce::String (sixteenth);
}

const std::vector<int>& metronomeVolumesDb()
{
    static const std::vector<int> v { 0, -3, -6, -12, -18, -24, -60 };
    return v;
}
const std::vector<int>& countInChoices()
{
    static const std::vector<int> v { 0, 1, 2, 4 };
    return v;
}
const std::vector<int>& fixedLengthChoices()
{
    static const std::vector<int> v { 0, 1, 2, 4, 8, 16 };
    return v;
}
const std::vector<GridChoice>& recordQuantiseChoices()
{
    static const std::vector<GridChoice> v { { Strings::recording::off, 0.0 },  { "1/4", 1.0 },         { "1/8", 0.5 },
                                             { "1/8T", 1.0 / 3.0 },             { "1/16", 0.25 },        { "1/16T", 1.0 / 6.0 },
                                             { "1/32", 0.125 } };
    return v;
}

juce::PopupMenu makeMetronomeMenu (ProjectModel& model)
{
    namespace R = Strings::recording;
    juce::PopupMenu menu;

    juce::PopupMenu volume;
    const double current = model.metronomeVolumeDb();
    for (const int db : metronomeVolumesDb())
    {
        const bool ticked = db <= -60 ? current <= ProjectModel::kMinMetronomeDb + 0.5 : std::abs (current - db) < 0.5;
        volume.addItem (R::volumeDb (db), true, ticked, [&model, db] { model.setMetronomeVolumeDb (db); });
    }
    menu.addSubMenu (R::menuVolume, volume);

    menu.addItem (R::menuOnlyRecording, true, model.metronomeOnlyRecording(),
                  [&model] { model.setMetronomeOnlyRecording (! model.metronomeOnlyRecording()); });

    juce::PopupMenu countIn;
    for (const int bars : countInChoices())
        countIn.addItem (bars == 0 ? R::none : R::bars (bars), true, model.countInBars() == bars, [&model, bars] { model.setCountInBars (bars); });
    menu.addSubMenu (R::menuCountIn, countIn);

    juce::PopupMenu sound;
    sound.addItem (R::soundClassic, true, model.metronomeSound() == "classic", [&model] { model.setMetronomeSound ("classic"); });
    sound.addItem (R::soundWood, true, model.metronomeSound() == "wood", [&model] { model.setMetronomeSound ("wood"); });
    menu.addSubMenu (R::menuClickSound, sound);
    return menu;
}

juce::PopupMenu makeRecordMenu (ProjectModel& model)
{
    namespace R = Strings::recording;
    juce::PopupMenu menu;

    juce::PopupMenu fixed;
    for (const int bars : fixedLengthChoices())
        fixed.addItem (bars == 0 ? R::off : R::bars (bars), true, model.recordFixedBars() == bars, [&model, bars] { model.setRecordFixedBars (bars); });
    menu.addSubMenu (R::menuFixedLength, fixed);

    juce::PopupMenu quantise;
    for (const auto& choice : recordQuantiseChoices())
    {
        const double beats = choice.beats;
        quantise.addItem (choice.label, true, std::abs (model.recordQuantise() - beats) < 1.0e-6, [&model, beats] { model.setRecordQuantise (beats); });
    }
    menu.addSubMenu (R::menuRecordQuantise, quantise);
    return menu;
}
} // namespace ui
