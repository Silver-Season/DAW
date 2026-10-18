#include "InputMenus.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "../model/AudioInput.h"

namespace inputmenus
{
namespace
{
// The user's name of a channel from the settings, if there is one.
juce::String channelName (AudioEngine& engine, int channel)
{
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    return device != nullptr ? engine.getSettings().channelName (device->getName(), true, channel) : juce::String();
}

juce::String describe (AudioEngine& engine, const AudioInputSpec& in)
{
    if (in.kind == AudioInputKind::none)
        return Strings::audioInput::none;
    if (in.kind == AudioInputKind::resample)
        return Strings::audioInput::resample;
    auto text = in.isStereo() ? Strings::audioInput::stereo (in.left, in.right) : Strings::audioInput::mono (in.left);
    auto name = channelName (engine, in.left);
    if (in.isStereo() && name.isEmpty())
        name = channelName (engine, in.right);
    return name.isNotEmpty() ? text + Strings::audioInput::separator + name : text;
}

void setInput (ProjectModel& model, const TrackRef& track, const juce::String& value)
{
    if (track.input() == value)
        return;
    model.undo.beginNewTransaction (Strings::audioInput::undoInput);
    track.setInput (value, &model.undo);
}
} // namespace

juce::String inputLabel (AudioEngine& engine, const TrackRef& track)
{
    if (track.isAudio())
        return describe (engine, AudioInputSpec::parse (track.input()));
    const auto input = track.input();
    return input == "keyboard"           ? Strings::detail::inputKeyboard
           : input == "none"             ? Strings::detail::inputNone
           : input.startsWith ("midi:")  ? Strings::detail::inputDevice
                                         : Strings::detail::inputAllMidi;
}

juce::String monitorLabel (const TrackRef& track)
{
    const auto monitor = track.monitor();
    return monitor == "in" ? Strings::detail::monitorIn : (monitor == "off" ? Strings::detail::monitorOff : Strings::detail::monitorAuto);
}

void showInputMenu (AudioEngine& engine, ProjectModel& model, const TrackRef& track, juce::Component& target)
{
    juce::PopupMenu menu;
    std::vector<juce::String> values;
    const auto add = [&] (const juce::String& text, const juce::String& value, bool enabled = true) {
        values.push_back (value);
        menu.addItem ((int) values.size(), text, enabled, track.input() == value);
    };

    if (track.isAudio())
    {
        const auto names = engine.getInputChannelNames();
        const auto current = AudioInputSpec::parse (track.input());
        juce::PopupMenu mono;
        for (int i = 0; i < names.size(); ++i)
        {
            AudioInputSpec s;
            s.left = i;
            s.right = -1;
            values.push_back (s.toString());
            mono.addItem ((int) values.size(), describe (engine, s), names[i].isNotEmpty(),
                          current.kind == AudioInputKind::device && ! current.isStereo() && current.left == i);
        }
        for (int i = 0; i + 1 < names.size(); i += 2)
        {
            AudioInputSpec s;
            s.left = i;
            s.right = i + 1;
            add (describe (engine, s), s.toString(), names[i].isNotEmpty() || names[i + 1].isNotEmpty());
        }
        if (names.isEmpty())
            menu.addItem (-1, Strings::audioInput::noInputs, false);
        menu.addSubMenu (Strings::audioInput::monoInputs, mono, names.size() > 0);
        menu.addSeparator();
        add (Strings::audioInput::resample, "resample");
        add (Strings::audioInput::none, "none");
    }
    else
    {
        add (Strings::detail::inputAllMidi, "all-midi");
        add (Strings::detail::inputKeyboard, "keyboard");
        add (Strings::detail::inputNone, "none");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [&model, id = track.id(), values] (int result) {
                            const auto t = model.findTrack (id);
                            if (result > 0 && result <= (int) values.size() && t.isValid())
                                setInput (model, t, values[(size_t) result - 1]);
                        });
}

void showMonitorMenu (ProjectModel& model, const TrackRef& track, juce::Component& target)
{
    juce::PopupMenu menu;
    const juce::StringArray values { "auto", "in", "off" };
    menu.addItem (1, Strings::audioInput::monitorAutoLong, true, track.monitor() == "auto");
    menu.addItem (2, Strings::audioInput::monitorInLong, true, track.monitor() == "in");
    menu.addItem (3, Strings::audioInput::monitorOffLong, true, track.monitor() == "off");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [&model, id = track.id(), values] (int result) {
        const auto t = model.findTrack (id);
        if (result < 1 || ! t.isValid() || t.monitor() == values[result - 1])
            return;
        model.undo.beginNewTransaction (Strings::audioInput::undoMonitor);
        t.setMonitor (values[result - 1], &model.undo);
    });
}
} // namespace inputmenus
