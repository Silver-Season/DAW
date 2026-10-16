#include "MainComponent.h"

#include "Strings.h"

#include "daw/engine/DemoContent.h"
#include "daw/synth/PrismaParams.h"

using namespace prisma_ui;

namespace
{
constexpr int kSceneColumnWidth = 96;
constexpr int kHeaderHeight = 46;
constexpr int kSlotHeight = 30;
constexpr int kStopRowHeight = 26;

// The model stores colours as palette indices; until the themes of task 1.8 arrive this is the "Prism" palette.
juce::Colour paletteColour (int index)
{
    static const float hues[16] { 350, 12, 24, 36, 48, 70, 110, 150, 170, 196, 214, 236, 262, 284, 306, 328 };
    return juce::Colour::fromHSL (hues[juce::jlimit (0, 15, index)] / 360.0f, 0.78f, 0.62f, 1.0f);
}

enum class DemoClip
{
    chords,
    arp,
    bass,
    longBass,
    lead
};

daw::engine::MidiClip makeDemoClip (DemoClip type)
{
    using namespace daw::engine::demo;
    switch (type)
    {
        case DemoClip::chords: return padClip();
        case DemoClip::arp: return arpClip();
        case DemoClip::bass: return bassClip (false);
        case DemoClip::longBass: return bassClip (true);
        case DemoClip::lead: return leadClip();
    }
    return {};
}

// Level meter with peak decay, fed from the engine's peak atomics.
class Meter final : public juce::Component
{
public:
    void push (float peakL, float peakR)
    {
        levelL = juce::jmax (peakL, levelL * 0.86f);
        levelR = juce::jmax (peakR, levelR * 0.86f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::background);
        g.fillRoundedRectangle (r, 2.0f);
        auto drawBar = [&] (juce::Rectangle<float> bar, float level) {
            const float db = juce::Decibels::gainToDecibels (level, -60.0f);
            const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
            auto filled = bar.removeFromBottom (bar.getHeight() * norm);
            juce::ColourGradient grad (juce::Colour (0xff48bb78), 0, r.getBottom(), juce::Colour (0xfff56565), 0, r.getY(), false);
            grad.addColour (0.75, juce::Colour (0xffecc94b));
            g.setGradientFill (grad);
            g.fillRect (filled);
        };
        auto left = r.reduced (1.0f).removeFromLeft (r.getWidth() * 0.5f - 1.0f);
        auto right = r.reduced (1.0f).removeFromRight (r.getWidth() * 0.5f - 1.0f);
        drawBar (left, levelL);
        drawBar (right, levelR);
    }

private:
    float levelL = 0.0f, levelR = 0.0f;
};
} // namespace

//==============================================================================
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessorEditor* editor, const juce::String& title, std::function<void()> onClose)
        : DocumentWindow (title, colours::background, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { closeCallback(); } // owner deletes this window

private:
    std::function<void()> closeCallback;
};


//==============================================================================
class MainComponent::TransportBar final : public juce::Component
{
public:
    TransportBar (AudioEngine& e, ProjectModel& m) : engine (e), model (m)
    {
        playButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6 Play"));
        playButton.setClickingTogglesState (false);
        playButton.onClick = [this] { engine.play(); };
        stopButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xa0 Stop"));
        stopButton.onClick = [this] { engine.stop(); };

        tempo.setSliderStyle (juce::Slider::LinearBar);
        tempo.setRange (40.0, 240.0, 0.1);
        tempo.setValue (model.tempo(), juce::dontSendNotification);
        tempo.setTextValueSuffix (" BPM");
        tempo.onDragStart = [this] { model.undo.beginNewTransaction (Strings::undoTempo); };
        tempo.onValueChange = [this] { model.setTempo (tempo.getValue()); };

        quantise.addItemList ({ "Launch: none", "Launch: 1/4", "Launch: 1/2", "Launch: 1 bar", "Launch: 2 bars" }, 1);
        quantise.onChange = [this] { model.setLaunchQuantise (quantiseValues[quantise.getSelectedId() - 1]); };

        position.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        position.setColour (juce::Label::textColourId, colours::text);
        position.setJustificationType (juce::Justification::centred);
        cpu.setJustificationType (juce::Justification::centredRight);

        for (auto* c : std::initializer_list<juce::Component*> { &playButton, &stopButton, &tempo, &quantise, &position, &cpu, &audioButton, &pluginsButton })
            addAndMakeVisible (c);
        update();
    }

    void update()
    {
        const double beats = engine.getPositionBeats();
        const int bar = (int) std::floor (beats / 4.0) + 1;
        const int beat = (int) std::floor (std::fmod (beats, 4.0)) + 1;
        const int sixteenth = (int) std::floor (std::fmod (beats * 4.0, 4.0)) + 1;
        position.setText (juce::String (bar) + "." + juce::String (beat) + "." + juce::String (sixteenth), juce::dontSendNotification);
        playButton.setColour (juce::TextButton::buttonColourId, engine.isPlaying() ? colours::accent.withAlpha (0.5f) : colours::panel);
        if (! tempo.isMouseButtonDown() && std::abs (tempo.getValue() - model.tempo()) > 0.01)
            tempo.setValue (model.tempo(), juce::dontSendNotification);
        for (int i = 0; i < 5; ++i)
            if (std::abs (quantiseValues[i] - model.launchQuantise()) < 1.0e-6 && quantise.getSelectedId() != i + 1)
                quantise.setSelectedId (i + 1, juce::dontSendNotification);
        cpu.setText ("CPU " + juce::String (engine.getDeviceManager().getCpuUsage() * 100.0, 1) + "%", juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colours::panel);
        g.fillRect (getLocalBounds());
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("DAW", getLocalBounds().removeFromLeft (70).reduced (12, 0), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        r.removeFromLeft (70);
        playButton.setBounds (r.removeFromLeft (80).reduced (2));
        stopButton.setBounds (r.removeFromLeft (80).reduced (2));
        r.removeFromLeft (10);
        tempo.setBounds (r.removeFromLeft (120).reduced (2, 4));
        r.removeFromLeft (10);
        position.setBounds (r.removeFromLeft (110));
        quantise.setBounds (r.removeFromLeft (140).reduced (2, 4));
        pluginsButton.setBounds (r.removeFromRight (100).reduced (2));
        audioButton.setBounds (r.removeFromRight (100).reduced (2));
        cpu.setBounds (r.removeFromRight (100));
    }

    juce::TextButton audioButton { "Audio/MIDI" }, pluginsButton { "Plugins" };

private:
    static constexpr double quantiseValues[5] { 0.0, 1.0, 2.0, 4.0, 8.0 };

    AudioEngine& engine;
    ProjectModel& model;
    juce::TextButton playButton, stopButton;
    juce::Slider tempo;
    juce::ComboBox quantise;
    juce::Label position, cpu;
};

//==============================================================================
class MainComponent::SessionGrid final : public juce::Component
{
public:
    SessionGrid (AudioEngine& e, ProjectModel& m) : engine (e), model (m) {}

    std::function<void (int64_t trackId)> onTrackHeaderClicked;

    int trackWidth() const { return (getWidth() - kSceneColumnWidth) / juce::jmax (1, model.numTracks()); }
    int preferredHeight() const { return kHeaderHeight + model.numScenes() * kSlotHeight + kStopRowHeight + 8; }

    // "Prisma: Glass Pad": the device name plus, for a Prisma, the preset it was last set to.
    juce::String instrumentLabel (const TrackRef& track) const
    {
        if (track.numDevices() == 0)
            return Strings::labelNoInstrument;
        const auto device = track.device (0);
        auto label = device.name();
        if (auto* processor = engine.findProcessor (device.id()); processor != nullptr && device.kind() == "prisma")
            label += ": " + processor->getProgramName (processor->getCurrentProgram());
        return label + juce::String::fromUTF8 (" \xe2\x96\xbe");
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::background);
        const int tw = trackWidth();
        const int numScenes = model.numScenes();
        const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;

        for (int ti = 0; ti < model.numTracks(); ++ti)
        {
            const auto track = model.trackAt (ti);
            const auto* runtime = engine.findRuntime (track.id());
            const int x = ti * tw;
            const auto trackColour = paletteColour (track.colour());

            auto header = juce::Rectangle<int> (x, 0, tw, kHeaderHeight).reduced (2);
            g.setColour (colours::panel);
            g.fillRoundedRectangle (header.toFloat(), 4.0f);
            g.setColour (trackColour);
            g.fillRect (header.removeFromTop (4));
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (track.name(), header.removeFromTop (20).reduced (6, 0), juce::Justification::centredLeft);
            g.setColour (colours::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (instrumentLabel (track), header.reduced (6, 0), juce::Justification::centredLeft, true);

            const int playingScene = runtime != nullptr ? runtime->playingSlot.load() : -1;
            const int queuedScene = runtime != nullptr ? runtime->queuedSlot.load() : -1;
            for (int s = 0; s < numScenes; ++s)
            {
                auto slot = juce::Rectangle<int> (x, kHeaderHeight + s * kSlotHeight, tw, kSlotHeight).reduced (2);
                const auto clip = track.clipAt (s);
                if (! clip.isValid())
                {
                    g.setColour (colours::panel.darker (0.3f));
                    g.fillRoundedRectangle (slot.toFloat(), 3.0f);
                    g.setColour (colours::panelEdge);
                    g.drawRoundedRectangle (slot.toFloat(), 3.0f, 1.0f);
                    continue;
                }
                const bool isPlaying = playingScene == s, isQueued = queuedScene == s;
                const auto colour = paletteColour (clip.colour());
                g.setColour (isPlaying ? colour : colour.withMultipliedBrightness (0.55f).withMultipliedSaturation (0.7f));
                if (isQueued && blink)
                    g.setColour (colour.brighter (0.4f));
                g.fillRoundedRectangle (slot.toFloat(), 3.0f);

                auto icon = slot.removeFromLeft (22).toFloat().reduced (7.0f, 8.0f);
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                if (isPlaying)
                {
                    juce::Path tri;
                    tri.addTriangle (icon.getX(), icon.getY(), icon.getX(), icon.getBottom(), icon.getRight(), icon.getCentreY());
                    g.fillPath (tri);
                }
                else
                    g.drawRect (icon, 1.5f);
                g.setFont (juce::FontOptions (12.0f));
                g.drawText (clip.name(), slot.reduced (2, 0), juce::Justification::centredLeft, true);
            }

            auto stopCell = juce::Rectangle<int> (x, kHeaderHeight + numScenes * kSlotHeight, tw, kStopRowHeight).reduced (2);
            g.setColour (colours::panel);
            g.fillRoundedRectangle (stopCell.toFloat(), 3.0f);
            g.setColour (playingScene >= 0 ? colours::text : colours::dimText);
            g.fillRect (stopCell.withSizeKeepingCentre (10, 10));
        }

        // Scene launchers
        const int sx = model.numTracks() * tw;
        g.setColour (colours::dimText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (Strings::labelScenes, juce::Rectangle<int> (sx, 0, kSceneColumnWidth, kHeaderHeight), juce::Justification::centred);
        for (int s = 0; s < numScenes; ++s)
        {
            auto cell = juce::Rectangle<int> (sx, kHeaderHeight + s * kSlotHeight, kSceneColumnWidth, kSlotHeight).reduced (2);
            g.setColour (colours::panel);
            g.fillRoundedRectangle (cell.toFloat(), 3.0f);
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb6 ") + model.sceneAt (s).name(), cell.reduced (6, 0), juce::Justification::centredLeft, true);
        }
        auto stopAll = juce::Rectangle<int> (sx, kHeaderHeight + numScenes * kSlotHeight, kSceneColumnWidth, kStopRowHeight).reduced (2);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (stopAll.toFloat(), 3.0f);
        g.setColour (colours::text);
        g.drawText (Strings::labelStopAll, stopAll, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int tw = trackWidth();
        const int x = e.x, y = e.y;
        const int trackIndex = x / juce::jmax (1, tw);
        const int row = (y - kHeaderHeight) / kSlotHeight;
        const int numScenes = model.numScenes();

        if (trackIndex >= model.numTracks()) // scene column
        {
            if (y >= kHeaderHeight && row < numScenes)
            {
                if (e.mods.isPopupMenu())
                    showSceneMenu (e, row);
                else
                    engine.launchScene (row);
            }
            else if (row >= numScenes)
                engine.stopAllClips();
            return;
        }

        const auto track = model.trackAt (trackIndex);
        if (y < kHeaderHeight)
        {
            model.setSelectedTrack (track.id());
            if (onTrackHeaderClicked)
                onTrackHeaderClicked (track.id());
            return;
        }

        if (row >= numScenes)
        {
            engine.stopTrack (track.id());
            return;
        }

        const bool hasClip = track.clipAt (row).isValid();
        if (e.mods.isPopupMenu() || ! hasClip)
        {
            showClipMenu (e, track.id(), row, hasClip);
            return;
        }

        engine.launchClip (track.id(), row);
    }

private:
    void showClipMenu (const juce::MouseEvent& e, int64_t trackId, int scene, bool hasClip)
    {
        juce::PopupMenu menu;
        juce::PopupMenu create;
        create.addItem (1, "Chords (4 bars)");
        create.addItem (2, "Arpeggio (4 bars)");
        create.addItem (3, "Bass, eighth notes");
        create.addItem (4, "Bass, sustained");
        create.addItem (5, "Lead melody");
        menu.addSubMenu (hasClip ? Strings::menuReplaceDemoClip : Strings::menuCreateDemoClip, create);
        if (hasClip)
        {
            menu.addItem (10, Strings::menuLaunch);
            menu.addItem (11, Strings::menuDeleteClip);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                            [this, trackId, scene] (int result) {
                                const auto track = model.findTrack (trackId);
                                if (! track.isValid() || scene >= model.numScenes())
                                    return;
                                if (result >= 1 && result <= 5)
                                {
                                    model.undo.beginNewTransaction (Strings::undoNewClip);
                                    const auto source = makeDemoClip ((DemoClip) (result - 1));
                                    auto clip = model.createClip (track, scene, source.lengthBeats, source.name);
                                    clip.setNotes (source.notes, &model.undo);
                                }
                                else if (result == 10)
                                    engine.launchClip (trackId, scene);
                                else if (result == 11)
                                {
                                    model.undo.beginNewTransaction (Strings::undoDeleteClip);
                                    model.deleteClip (track, scene);
                                }
                            });
    }

    void showSceneMenu (const juce::MouseEvent& e, int scene)
    {
        juce::PopupMenu menu;
        menu.addItem (1, Strings::menuAddScene);
        menu.addItem (2, Strings::menuDeleteScene, model.numScenes() > 1);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                            [this, scene] (int result) {
                                if (result == 1)
                                {
                                    model.undo.beginNewTransaction (Strings::undoNewScene);
                                    model.insertScene (scene + 1);
                                }
                                else if (result == 2 && scene < model.numScenes())
                                {
                                    model.undo.beginNewTransaction (Strings::undoDeleteScene);
                                    model.deleteScene (scene);
                                }
                            });
    }

    AudioEngine& engine;
    ProjectModel& model;
};

//==============================================================================
class MainComponent::MixerStrip final : public juce::Component
{
public:
    MixerStrip (AudioEngine& e, ProjectModel& m, int64_t track) : engine (e), model (m), trackId (track)
    {
        const auto t = model.findTrack (trackId);

        volume.setSliderStyle (juce::Slider::LinearVertical);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        volume.setTextValueSuffix (" dB");
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setValue (t.volumeDb(), juce::dontSendNotification);
        volume.onDragStart = [this] { model.undo.beginNewTransaction (Strings::undoTrackVolume); };
        volume.onValueChange = [this] {
            model.findTrack (trackId).setVolumeDb ((float) volume.getValue(), &model.undo);
        };

        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setPopupDisplayEnabled (true, true, nullptr);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setValue (t.pan(), juce::dontSendNotification);
        pan.onDragStart = [this] { model.undo.beginNewTransaction (Strings::undoTrackPan); };
        pan.onValueChange = [this] { model.findTrack (trackId).setPan ((float) pan.getValue(), &model.undo); };

        auto setupToggle = [this] (juce::TextButton& b, const juce::String& text, juce::Colour on) {
            b.setButtonText (text);
            b.setClickingTogglesState (true);
            b.setColour (juce::TextButton::buttonOnColourId, on);
            addAndMakeVisible (b);
        };
        setupToggle (mute, "M", juce::Colour (0xfff6ad55));
        setupToggle (solo, "S", juce::Colour (0xff63b3ed));
        setupToggle (arm, juce::String::fromUTF8 ("\xe2\x97\x8f"), juce::Colour (0xfff56565));
        mute.onClick = [this] {
            model.undo.beginNewTransaction (Strings::undoTrackMute);
            model.findTrack (trackId).setMute (mute.getToggleState(), &model.undo);
        };
        solo.onClick = [this] {
            model.undo.beginNewTransaction (Strings::undoTrackSolo);
            model.findTrack (trackId).setSolo (solo.getToggleState(), &model.undo);
        };
        arm.onClick = [this] {
            // Only one track is armed at a time here; Cmd-click to arm several comes with the new layout.
            const bool on = arm.getToggleState();
            for (const auto& other : model.tracks())
                other.setArm (on && other.id() == trackId, nullptr);
        };
        arm.setTooltip (Strings::tipArm);

        addAndMakeVisible (volume);
        addAndMakeVisible (pan);
        addAndMakeVisible (meter);
    }

    int64_t getTrackId() const { return trackId; }

    void update()
    {
        const auto t = model.findTrack (trackId);
        if (! t.isValid())
            return;
        if (auto* rt = engine.findRuntime (trackId))
            meter.push (rt->meterL.exchange (0.0f), rt->meterR.exchange (0.0f));
        arm.setToggleState (t.arm(), juce::dontSendNotification);
        mute.setToggleState (t.mute(), juce::dontSendNotification);
        solo.setToggleState (t.solo(), juce::dontSendNotification);
        if (! volume.isMouseButtonDown() && std::abs (volume.getValue() - t.volumeDb()) > 0.05)
            volume.setValue (t.volumeDb(), juce::dontSendNotification);
        if (! pan.isMouseButtonDown() && std::abs (pan.getValue() - t.pan()) > 0.005)
            pan.setValue (t.pan(), juce::dontSendNotification);
        if (name != t.name() || colourIndex != t.colour())
        {
            name = t.name();
            colourIndex = t.colour();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (2).toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (paletteColour (colourIndex));
        g.fillRect (r.removeFromTop (3.0f));
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (name, r.removeFromTop (18.0f), juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        r.removeFromTop (20);
        auto buttons = r.removeFromBottom (24);
        const int bw = buttons.getWidth() / 3;
        mute.setBounds (buttons.removeFromLeft (bw).reduced (1));
        solo.setBounds (buttons.removeFromLeft (bw).reduced (1));
        arm.setBounds (buttons.reduced (1));
        pan.setBounds (r.removeFromTop (38));
        meter.setBounds (r.removeFromRight (12).withTrimmedBottom (20));
        volume.setBounds (r);
    }

private:
    AudioEngine& engine;
    ProjectModel& model;
    const int64_t trackId;
    juce::String name;
    int colourIndex = 0;
    juce::Slider volume, pan;
    juce::TextButton mute, solo, arm;
    Meter meter;
};

class MainComponent::MasterStrip final : public juce::Component
{
public:
    MasterStrip (AudioEngine& e, ProjectModel& m) : engine (e), model (m)
    {
        volume.setSliderStyle (juce::Slider::LinearVertical);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        volume.setTextValueSuffix (" dB");
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setValue (model.masterVolumeDb(), juce::dontSendNotification);
        volume.onDragStart = [this] { model.undo.beginNewTransaction (Strings::undoMasterVolume); };
        volume.onValueChange = [this] { model.setMasterVolumeDb ((float) volume.getValue()); };
        addAndMakeVisible (volume);
        addAndMakeVisible (meter);
    }

    void update()
    {
        meter.push (engine.masterMeterL.exchange (0.0f), engine.masterMeterR.exchange (0.0f));
        if (! volume.isMouseButtonDown() && std::abs (volume.getValue() - model.masterVolumeDb()) > 0.05)
            volume.setValue (model.masterVolumeDb(), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (2).toFloat();
        g.setColour (colours::panel.brighter (0.05f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (colours::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (Strings::labelMaster, r.removeFromTop (22.0f), juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        r.removeFromTop (22);
        meter.setBounds (r.removeFromRight (16).withTrimmedBottom (20));
        volume.setBounds (r);
    }

private:
    AudioEngine& engine;
    ProjectModel& model;
    juce::Slider volume;
    Meter meter;
};

//==============================================================================
MainComponent::MainComponent (AudioEngine& e, ProjectModel& m)
    : engine (e), model (m), keyboard (e.getKeyboardState())
{
    setLookAndFeel (&lnf);

    transportBar = std::make_unique<TransportBar> (engine, model);
    transportBar->audioButton.onClick = [this] { showAudioSettings(); };
    transportBar->pluginsButton.onClick = [this] { showPluginScanner(); };
    addAndMakeVisible (*transportBar);

    sessionGrid = std::make_unique<SessionGrid> (engine, model);
    sessionGrid->onTrackHeaderClicked = [this] (int64_t trackId) { showInstrumentMenu (trackId); };
    addAndMakeVisible (*sessionGrid);

    masterStrip = std::make_unique<MasterStrip> (engine, model);
    addAndMakeVisible (*masterStrip);

    addAndMakeVisible (keyboard);

    model.root.addListener (this);
    rebuildStrips();

    setWantsKeyboardFocus (true);
    setSize (1280, 860);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    model.root.removeListener (this);
    editorWindows.clear();
    scannerWindow.reset();
    setLookAndFeel (nullptr);
}

void MainComponent::structureChanged (const juce::ValueTree& parent)
{
    if (parent.hasType (ids::TRACKS) || parent.hasType (ids::SCENES))
        layoutDirty = true;
}

void MainComponent::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    structureChanged (parent);
    closeEditorsOfMissingDevices();
}

void MainComponent::rebuildStrips()
{
    strips.clear();
    for (const auto& t : model.tracks())
    {
        strips.push_back (std::make_unique<MixerStrip> (engine, model, t.id()));
        addAndMakeVisible (*strips.back());
    }
    layoutDirty = false;
    resized();
}

void MainComponent::closeEditorsOfMissingDevices()
{
    for (auto it = editorWindows.begin(); it != editorWindows.end();)
        it = model.findDevice (it->first).isValid() ? std::next (it) : editorWindows.erase (it);
}

//==============================================================================
void MainComponent::setPrismaInstrument (int64_t trackId, const juce::String& presetName)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid())
        return;

    PrismaProcessor temp;
    const auto& presets = daw::synth::factoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presetName == juce::String (presets[i].name))
            temp.setCurrentProgram ((int) i);
    juce::MemoryBlock state;
    temp.getStateInformation (state);

    model.undo.beginNewTransaction (Strings::undoSetInstrument);
    removeInstrument (trackId);
    auto device = model.addDevice (track, "prisma", "Prisma", 0);
    device.setStateBase64 (state.toBase64Encoding(), &model.undo);
}

void MainComponent::removeInstrument (int64_t trackId)
{
    const auto track = model.findTrack (trackId);
    if (track.isValid() && track.numDevices() > 0)
    {
        engine.removeDevice (track.device (0)); // the model listener closes its editor window
    }
}

void MainComponent::loadPluginInstrument (int64_t trackId, const juce::PluginDescription& desc)
{
    engine.loadPluginInstrument (trackId, desc, [this] (juce::String error, int64_t deviceId) {
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, Strings::dialogPluginTitle, error);
        else
            openInstrumentEditor (deviceId);
    });
}

void MainComponent::showInstrumentMenu (int64_t trackId)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid())
        return;
    const bool hasInstrument = track.numDevices() > 0;

    juce::PopupMenu menu;
    menu.addSectionHeader (track.name());
    menu.addItem (1, Strings::menuOpenEditor, hasInstrument && engine.findProcessor (track.device (0).id()) != nullptr);
    menu.addSeparator();

    juce::PopupMenu prismaMenu;
    const auto& presets = daw::synth::factoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        prismaMenu.addItem (1000 + i, presets[(size_t) i].name);
    menu.addSubMenu (Strings::menuPrisma, prismaMenu);

    juce::PopupMenu pluginMenu;
    auto types = engine.getKnownPlugins().getTypes();
    std::vector<juce::PluginDescription> instruments;
    for (auto& d : types)
        if (d.isInstrument)
            instruments.push_back (d);
    std::sort (instruments.begin(), instruments.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    for (int i = 0; i < (int) instruments.size(); ++i)
        pluginMenu.addItem (2000 + i, instruments[(size_t) i].name + "  (" + instruments[(size_t) i].manufacturerName + ", "
                                          + instruments[(size_t) i].pluginFormatName + ")");
    if (instruments.empty())
        pluginMenu.addItem (-1, Strings::menuNoPlugins, false);
    menu.addSubMenu (Strings::menuPlugins, pluginMenu);
    menu.addItem (3, Strings::menuScanPlugins);
    menu.addSeparator();
    menu.addItem (4, Strings::menuRemoveInstrument, hasInstrument);
    menu.addSeparator();
    menu.addItem (5, Strings::menuAddMidiTrack);
    menu.addItem (6, Strings::menuDeleteTrack, model.numTracks() > 1);

    menu.showMenuAsync (juce::PopupMenu::Options(), [this, trackId, instruments, &presets] (int result) {
        const auto t = model.findTrack (trackId);
        if (! t.isValid())
            return;
        if (result == 1 && t.numDevices() > 0)
            openInstrumentEditor (t.device (0).id());
        else if (result == 3)
            showPluginScanner();
        else if (result == 4)
        {
            model.undo.beginNewTransaction (Strings::undoRemoveInstrument);
            removeInstrument (trackId);
        }
        else if (result == 5)
        {
            model.undo.beginNewTransaction (Strings::undoNewTrack);
            auto added = model.addTrack ("midi", model.indexOfTrack (trackId) + 1);
            model.addDevice (added, "prisma", "Prisma");
        }
        else if (result == 6)
        {
            model.undo.beginNewTransaction (Strings::undoDeleteTrack);
            engine.deleteTrack (trackId);
        }
        else if (result >= 1000 && result < 2000)
            setPrismaInstrument (trackId, presets[(size_t) (result - 1000)].name);
        else if (result >= 2000)
            loadPluginInstrument (trackId, instruments[(size_t) (result - 2000)]);
    });
}

void MainComponent::openInstrumentEditor (int64_t deviceId)
{
    auto* processor = engine.findProcessor (deviceId);
    if (processor == nullptr)
        return;
    if (auto it = editorWindows.find (deviceId); it != editorWindows.end())
    {
        it->second->toFront (true);
        return;
    }
    juce::AudioProcessorEditor* editor = processor->hasEditor() ? processor->createEditorIfNeeded() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (*processor);
    editorWindows[deviceId] = std::make_unique<PluginWindow> (editor, processor->getName(), [this, deviceId] {
        // Defer deletion: this runs from the window's own close button.
        juce::MessageManager::callAsync ([this, alive = juce::Component::SafePointer<MainComponent> (this), deviceId] {
            if (alive != nullptr)
                closeInstrumentEditor (deviceId);
        });
    });
}

void MainComponent::closeInstrumentEditor (int64_t deviceId)
{
    editorWindows.erase (deviceId);
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 2, 0, 2, true, false, true, false);
    selector->setSize (520, 480);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = Strings::windowAudioSettings;
    options.dialogBackgroundColour = colours::panel;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainComponent::showPluginScanner()
{
    if (scannerWindow != nullptr)
    {
        scannerWindow->toFront (true);
        return;
    }

    auto& props = engine.getSettings().getPropertiesFile();
    auto deadMansPedal = props.getFile().getSiblingFile ("RecentlyCrashedPluginsList");
    auto* list = new juce::PluginListComponent (engine.getFormatManager(), engine.getKnownPlugins(), deadMansPedal, &props, true);
    list->setSize (760, 520);

    struct ScannerWindow final : juce::DocumentWindow
    {
        ScannerWindow (juce::Component* content, std::function<void()> onClose)
            : DocumentWindow (Strings::windowPlugins, colours::panel, DocumentWindow::closeButton), close (std::move (onClose))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { close(); }
        std::function<void()> close;
    };

    scannerWindow = std::make_unique<ScannerWindow> (list, [this] {
        juce::MessageManager::callAsync ([this] { scannerWindow.reset(); });
    });
}

void MainComponent::timerCallback()
{
    if (layoutDirty)
        rebuildStrips();
    transportBar->update();
    for (auto& s : strips)
        s->update();
    masterStrip->update();
    sessionGrid->repaint();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        if (engine.isPlaying())
            engine.stop();
        else
            engine.play();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        model.undo.undo();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('Z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        model.undo.redo();
        return true;
    }
    return keyboard.handleKeyPressed (key);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    transportBar->setBounds (r.removeFromTop (48));
    keyboard.setBounds (r.removeFromBottom (72));

    auto grid = r.removeFromTop (sessionGrid->preferredHeight()).reduced (4);
    auto mixer = r.reduced (4, 2);
    sessionGrid->setBounds (grid);

    const int tw = sessionGrid->trackWidth();
    for (int i = 0; i < (int) strips.size(); ++i)
        strips[(size_t) i]->setBounds (mixer.getX() + i * tw, mixer.getY(), tw, mixer.getHeight());
    masterStrip->setBounds (mixer.getX() + (int) strips.size() * tw, mixer.getY(), kSceneColumnWidth, mixer.getHeight());
}
