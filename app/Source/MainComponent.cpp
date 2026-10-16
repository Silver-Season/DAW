#include "MainComponent.h"
#include "../../plugins/prisma/Source/PrismaProcessor.h"

#include "daw/engine/DemoContent.h"
#include "daw/synth/PrismaParams.h"

using namespace prisma_ui;

namespace
{
constexpr int kSceneColumnWidth = 96;
constexpr int kHeaderHeight = 46;
constexpr int kSlotHeight = 30;
constexpr int kStopRowHeight = 26;

int presetIndex (const juce::String& name)
{
    const auto& presets = daw::synth::factoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (name == juce::String (presets[i].name))
            return (int) i;
    return 0;
}

enum class DemoClip
{
    Chords,
    Arp,
    Bass,
    LongBass,
    Lead
};

std::unique_ptr<daw::engine::MidiClip> makeDemoClip (DemoClip type, juce::Colour colour)
{
    using namespace daw::engine::demo;
    daw::engine::MidiClip clip;
    switch (type)
    {
        case DemoClip::Chords: clip = padClip(); break;
        case DemoClip::Arp: clip = arpClip(); break;
        case DemoClip::Bass: clip = bassClip (false); break;
        case DemoClip::LongBass: clip = bassClip (true); break;
        case DemoClip::Lead: clip = leadClip(); break;
    }
    clip.colour = colour.getARGB();
    return std::make_unique<daw::engine::MidiClip> (std::move (clip));
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
    explicit TransportBar (AudioEngine& e) : engine (e)
    {
        playButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6 Play"));
        playButton.setClickingTogglesState (false);
        playButton.onClick = [this] { engine.play(); };
        stopButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xa0 Stop"));
        stopButton.onClick = [this] { engine.stop(); };

        tempo.setSliderStyle (juce::Slider::LinearBar);
        tempo.setRange (40.0, 240.0, 0.1);
        tempo.setValue (engine.getTempo(), juce::dontSendNotification);
        tempo.setTextValueSuffix (" BPM");
        tempo.onValueChange = [this] { engine.setTempo (tempo.getValue()); };

        quantise.addItemList ({ "Launch: none", "Launch: 1/4", "Launch: 1/2", "Launch: 1 bar", "Launch: 2 bars" }, 1);
        quantise.setSelectedId (4, juce::dontSendNotification);
        quantise.onChange = [this] {
            const double values[] { 0.0, 1.0, 2.0, 4.0, 8.0 };
            engine.setLaunchQuantise (values[quantise.getSelectedId() - 1]);
        };

        position.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        position.setColour (juce::Label::textColourId, colours::text);
        position.setJustificationType (juce::Justification::centred);
        cpu.setJustificationType (juce::Justification::centredRight);

        for (auto* c : std::initializer_list<juce::Component*> { &playButton, &stopButton, &tempo, &quantise, &position, &cpu, &audioButton, &pluginsButton })
            addAndMakeVisible (c);
    }

    void update()
    {
        const double beats = engine.getPositionBeats();
        const int bar = (int) std::floor (beats / 4.0) + 1;
        const int beat = (int) std::floor (std::fmod (beats, 4.0)) + 1;
        const int sixteenth = (int) std::floor (std::fmod (beats * 4.0, 4.0)) + 1;
        position.setText (juce::String (bar) + "." + juce::String (beat) + "." + juce::String (sixteenth), juce::dontSendNotification);
        playButton.setColour (juce::TextButton::buttonColourId, engine.isPlaying() ? colours::accent.withAlpha (0.5f) : colours::panel);
        if (! tempo.isMouseButtonDown() && std::abs (tempo.getValue() - engine.getTempo()) > 0.01)
            tempo.setValue (engine.getTempo(), juce::dontSendNotification);
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
    AudioEngine& engine;
    juce::TextButton playButton, stopButton;
    juce::Slider tempo;
    juce::ComboBox quantise;
    juce::Label position, cpu;
};

//==============================================================================
class MainComponent::SessionGrid final : public juce::Component
{
public:
    explicit SessionGrid (AudioEngine& e) : engine (e) {}

    std::function<void (int track)> onTrackHeaderClicked;

    int trackWidth() const { return (getWidth() - kSceneColumnWidth) / AudioEngine::kNumTracks; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::background);
        const int tw = trackWidth();
        const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;

        for (int t = 0; t < AudioEngine::kNumTracks; ++t)
        {
            auto& track = engine.getTrack (t);
            const int x = t * tw;

            auto header = juce::Rectangle<int> (x, 0, tw, kHeaderHeight).reduced (2);
            g.setColour (colours::panel);
            g.fillRoundedRectangle (header.toFloat(), 4.0f);
            g.setColour (track.colour);
            g.fillRect (header.removeFromTop (4));
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (track.name, header.removeFromTop (20).reduced (6, 0), juce::Justification::centredLeft);
            g.setColour (colours::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (track.instrumentName.isEmpty() ? "(no instrument)" : track.instrumentName + juce::String::fromUTF8 (" \xe2\x96\xbe"),
                        header.reduced (6, 0), juce::Justification::centredLeft, true);

            const int playingScene = engine.getPlayingScene (t), queuedScene = engine.getQueuedScene (t);
            for (int s = 0; s < AudioEngine::kNumScenes; ++s)
            {
                auto slot = juce::Rectangle<int> (x, kHeaderHeight + s * kSlotHeight, tw, kSlotHeight).reduced (2);
                const auto* clip = track.clips[(size_t) s].get();
                if (clip == nullptr)
                {
                    g.setColour (colours::panel.darker (0.3f));
                    g.fillRoundedRectangle (slot.toFloat(), 3.0f);
                    g.setColour (colours::panelEdge);
                    g.drawRoundedRectangle (slot.toFloat(), 3.0f, 1.0f);
                    continue;
                }
                const bool isPlaying = playingScene == s, isQueued = queuedScene == s;
                const auto colour = juce::Colour (clip->colour);
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
                g.drawText (clip->name, slot.reduced (2, 0), juce::Justification::centredLeft, true);
            }

            auto stopCell = juce::Rectangle<int> (x, kHeaderHeight + AudioEngine::kNumScenes * kSlotHeight, tw, kStopRowHeight).reduced (2);
            g.setColour (colours::panel);
            g.fillRoundedRectangle (stopCell.toFloat(), 3.0f);
            g.setColour (playingScene >= 0 ? colours::text : colours::dimText);
            g.fillRect (stopCell.withSizeKeepingCentre (10, 10));
        }

        // Scene launchers
        const int sx = AudioEngine::kNumTracks * tw;
        g.setColour (colours::dimText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("SCENES", juce::Rectangle<int> (sx, 0, kSceneColumnWidth, kHeaderHeight), juce::Justification::centred);
        for (int s = 0; s < AudioEngine::kNumScenes; ++s)
        {
            auto cell = juce::Rectangle<int> (sx, kHeaderHeight + s * kSlotHeight, kSceneColumnWidth, kSlotHeight).reduced (2);
            g.setColour (colours::panel);
            g.fillRoundedRectangle (cell.toFloat(), 3.0f);
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb6 Scene ") + juce::String (s + 1), cell.reduced (6, 0), juce::Justification::centredLeft);
        }
        auto stopAll = juce::Rectangle<int> (sx, kHeaderHeight + AudioEngine::kNumScenes * kSlotHeight, kSceneColumnWidth, kStopRowHeight).reduced (2);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (stopAll.toFloat(), 3.0f);
        g.setColour (colours::text);
        g.drawText ("Stop all", stopAll, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int tw = trackWidth();
        const int x = e.x, y = e.y;
        const int track = x / juce::jmax (1, tw);
        const int row = (y - kHeaderHeight) / kSlotHeight;

        if (track >= AudioEngine::kNumTracks) // scene column
        {
            if (y >= kHeaderHeight && row < AudioEngine::kNumScenes)
                engine.launchScene (row);
            else if (row >= AudioEngine::kNumScenes)
                engine.stopAllClips();
            return;
        }

        if (y < kHeaderHeight)
        {
            if (onTrackHeaderClicked)
                onTrackHeaderClicked (track);
            return;
        }

        if (row >= AudioEngine::kNumScenes)
        {
            engine.stopTrack (track);
            return;
        }

        auto& t = engine.getTrack (track);
        const bool hasClip = t.clips[(size_t) row] != nullptr;

        if (e.mods.isPopupMenu() || ! hasClip)
        {
            juce::PopupMenu menu;
            juce::PopupMenu create;
            create.addItem (1, "Chords (4 bars)");
            create.addItem (2, "Arpeggio (4 bars)");
            create.addItem (3, "Bass, eighth notes");
            create.addItem (4, "Bass, sustained");
            create.addItem (5, "Lead melody");
            menu.addSubMenu (hasClip ? "Replace with demo clip" : "Create demo clip", create);
            if (hasClip)
            {
                menu.addItem (10, "Launch");
                menu.addItem (11, "Delete clip");
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                                [this, track, row] (int result) {
                                    auto colour = engine.getTrack (track).colour;
                                    if (result >= 1 && result <= 5)
                                        engine.setClip (track, row, makeDemoClip ((DemoClip) (result - 1), colour));
                                    else if (result == 10)
                                        engine.launchClip (track, row);
                                    else if (result == 11)
                                        engine.setClip (track, row, nullptr);
                                    repaint();
                                });
            return;
        }

        engine.launchClip (track, row);
    }

private:
    AudioEngine& engine;
};

//==============================================================================
class MainComponent::MixerStrip final : public juce::Component
{
public:
    MixerStrip (AudioEngine& e, int index) : engine (e), trackIndex (index)
    {
        auto& t = engine.getTrack (trackIndex);

        volume.setSliderStyle (juce::Slider::LinearVertical);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        volume.setTextValueSuffix (" dB");
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setValue (t.volumeDb.load(), juce::dontSendNotification);
        volume.onValueChange = [this] { engine.getTrack (trackIndex).volumeDb = (float) volume.getValue(); };

        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setPopupDisplayEnabled (true, true, nullptr);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setValue (t.pan.load(), juce::dontSendNotification);
        pan.onValueChange = [this] { engine.getTrack (trackIndex).pan = (float) pan.getValue(); };

        auto setupToggle = [this] (juce::TextButton& b, const juce::String& text, juce::Colour on) {
            b.setButtonText (text);
            b.setClickingTogglesState (true);
            b.setColour (juce::TextButton::buttonOnColourId, on);
            addAndMakeVisible (b);
        };
        setupToggle (mute, "M", juce::Colour (0xfff6ad55));
        setupToggle (solo, "S", juce::Colour (0xff63b3ed));
        setupToggle (arm, juce::String::fromUTF8 ("\xe2\x97\x8f"), juce::Colour (0xfff56565));
        mute.onClick = [this] { engine.getTrack (trackIndex).mute = mute.getToggleState(); };
        solo.onClick = [this] { engine.getTrack (trackIndex).solo = solo.getToggleState(); };
        arm.onClick = [this] { engine.armExclusive (arm.getToggleState() ? trackIndex : -1); };
        arm.setTooltip ("Arm: MIDI keyboards and the on-screen keyboard play this track");

        addAndMakeVisible (volume);
        addAndMakeVisible (pan);
        addAndMakeVisible (meter);
    }

    void update()
    {
        auto& t = engine.getTrack (trackIndex);
        meter.push (t.meterL.exchange (0.0f), t.meterR.exchange (0.0f));
        arm.setToggleState (t.arm.load(), juce::dontSendNotification);
        if (! volume.isMouseButtonDown() && std::abs (volume.getValue() - t.volumeDb.load()) > 0.05)
            volume.setValue (t.volumeDb.load(), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (2).toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 4.0f);
        auto& t = engine.getTrack (trackIndex);
        g.setColour (t.colour);
        g.fillRect (r.removeFromTop (3.0f));
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (t.name, r.removeFromTop (18.0f), juce::Justification::centred);
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
    const int trackIndex;
    juce::Slider volume, pan;
    juce::TextButton mute, solo, arm;
    Meter meter;
};

class MainComponent::MasterStrip final : public juce::Component
{
public:
    explicit MasterStrip (AudioEngine& e) : engine (e)
    {
        volume.setSliderStyle (juce::Slider::LinearVertical);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        volume.setTextValueSuffix (" dB");
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setValue (0.0, juce::dontSendNotification);
        volume.onValueChange = [this] { engine.masterVolumeDb = (float) volume.getValue(); };
        addAndMakeVisible (volume);
        addAndMakeVisible (meter);
    }

    void update() { meter.push (engine.masterMeterL.exchange (0.0f), engine.masterMeterR.exchange (0.0f)); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (2).toFloat();
        g.setColour (colours::panel.brighter (0.05f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (colours::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("MASTER", r.removeFromTop (22.0f), juce::Justification::centred);
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
    juce::Slider volume;
    Meter meter;
};

//==============================================================================
MainComponent::MainComponent (AudioEngine& e)
    : engine (e), keyboard (e.getKeyboardState())
{
    setLookAndFeel (&lnf);

    transportBar = std::make_unique<TransportBar> (engine);
    transportBar->audioButton.onClick = [this] { showAudioSettings(); };
    transportBar->pluginsButton.onClick = [this] { showPluginScanner(); };
    addAndMakeVisible (*transportBar);

    sessionGrid = std::make_unique<SessionGrid> (engine);
    sessionGrid->onTrackHeaderClicked = [this] (int t) { showInstrumentMenu (t); };
    addAndMakeVisible (*sessionGrid);

    for (int i = 0; i < AudioEngine::kNumTracks; ++i)
    {
        strips.push_back (std::make_unique<MixerStrip> (engine, i));
        addAndMakeVisible (*strips.back());
    }
    masterStrip = std::make_unique<MasterStrip> (engine);
    addAndMakeVisible (*masterStrip);

    addAndMakeVisible (keyboard);

    setWantsKeyboardFocus (true);
    setSize (1280, 860);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    for (auto& w : editorWindows)
        w.reset();
    scannerWindow.reset();
    setLookAndFeel (nullptr);
}

void MainComponent::populateDemoSession()
{
    struct TrackSetup
    {
        const char* name;
        const char* preset;
        float volume;
        std::vector<std::pair<int, DemoClip>> clips; // scene, clip
    };
    const std::vector<TrackSetup> setup {
        { "Pad", "Glass Pad", -4.0f, { { 0, DemoClip::Chords }, { 1, DemoClip::Chords }, { 2, DemoClip::Chords } } },
        { "Arp", "Pluck", -9.0f, { { 0, DemoClip::Arp }, { 1, DemoClip::Arp } } },
        { "Reese", "Reese Bass", -5.0f, { { 0, DemoClip::Bass } } },
        { "Lead", "Supersaw Lead", -10.0f, { { 1, DemoClip::Lead } } },
        { "Wobble", "Wobble Bass", -6.0f, { { 1, DemoClip::LongBass } } },
        { "Vox", "Vox Sweep", -8.0f, { { 2, DemoClip::Lead } } },
        { "Track 7", "Init", -6.0f, {} },
        { "Track 8", "Init", -6.0f, {} },
    };

    for (int i = 0; i < AudioEngine::kNumTracks && i < (int) setup.size(); ++i)
    {
        auto& t = engine.getTrack (i);
        t.name = setup[(size_t) i].name;
        t.volumeDb = setup[(size_t) i].volume;
        setBuiltInInstrument (i, presetIndex (setup[(size_t) i].preset));
        for (auto& [scene, type] : setup[(size_t) i].clips)
            engine.setClip (i, scene, makeDemoClip (type, t.colour));
    }
    engine.armExclusive (3);
    engine.setTempo (124.0);
}

void MainComponent::setBuiltInInstrument (int track, int preset)
{
    closeInstrumentEditor (track);
    auto prisma = std::make_unique<PrismaProcessor>();
    prisma->setCurrentProgram (preset);
    const auto name = "Prisma: " + prisma->getProgramName (preset);
    engine.setInstrument (track, std::move (prisma), name);
}

void MainComponent::showInstrumentMenu (int track)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (engine.getTrack (track).name);
    menu.addItem (1, "Open instrument editor", engine.getTrack (track).instrument != nullptr);
    menu.addSeparator();

    juce::PopupMenu prismaMenu;
    const auto& presets = daw::synth::factoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        prismaMenu.addItem (1000 + i, presets[(size_t) i].name);
    menu.addSubMenu ("Prisma (built-in wavetable synth)", prismaMenu);

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
        pluginMenu.addItem (-1, "No plugins found yet: use Scan", false);
    menu.addSubMenu ("AU / VST3 instruments", pluginMenu);
    menu.addItem (3, "Scan for plugins...");
    menu.addSeparator();
    menu.addItem (4, "Remove instrument");

    menu.showMenuAsync (juce::PopupMenu::Options(), [this, track, instruments] (int result) {
        if (result == 1)
            openInstrumentEditor (track);
        else if (result == 3)
            showPluginScanner();
        else if (result == 4)
        {
            closeInstrumentEditor (track);
            engine.setInstrument (track, nullptr, {});
        }
        else if (result >= 1000 && result < 2000)
            setBuiltInInstrument (track, result - 1000);
        else if (result >= 2000)
        {
            closeInstrumentEditor (track);
            const auto desc = instruments[(size_t) (result - 2000)];
            engine.loadPluginInstrument (track, desc, [this, track] (juce::String error) {
                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugin", error);
                else
                    openInstrumentEditor (track);
                sessionGrid->repaint();
            });
        }
        sessionGrid->repaint();
    });
}

void MainComponent::openInstrumentEditor (int track)
{
    auto& t = engine.getTrack (track);
    if (t.instrument == nullptr)
        return;
    if (auto& w = editorWindows[(size_t) track])
    {
        w->toFront (true);
        return;
    }
    juce::AudioProcessorEditor* editor = t.instrument->hasEditor() ? t.instrument->createEditorIfNeeded() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (*t.instrument);
    editorWindows[(size_t) track] = std::make_unique<PluginWindow> (editor, t.name + ": " + t.instrumentName,
                                                                   [this, track] { closeInstrumentEditor (track); });
}

void MainComponent::closeInstrumentEditor (int track)
{
    auto& w = editorWindows[(size_t) track];
    if (w == nullptr)
        return;
    // Defer deletion: this can be called from the window's own close button.
    auto* raw = w.release();
    juce::MessageManager::callAsync ([raw] { delete raw; });
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 2, 0, 2, true, false, true, false);
    selector->setSize (520, 480);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "Audio / MIDI settings";
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

    auto deadMansPedal = engine.getSettings()->getFile().getSiblingFile ("RecentlyCrashedPluginsList");
    auto* list = new juce::PluginListComponent (engine.getFormatManager(), engine.getKnownPlugins(), deadMansPedal, engine.getSettings(), true);
    list->setSize (760, 520);

    struct ScannerWindow final : juce::DocumentWindow
    {
        ScannerWindow (juce::Component* content, std::function<void()> onClose)
            : DocumentWindow ("Plugins (AU / VST3)", colours::panel, DocumentWindow::closeButton), close (std::move (onClose))
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

    const int gridHeight = kHeaderHeight + AudioEngine::kNumScenes * kSlotHeight + kStopRowHeight + 8;
    auto grid = r.removeFromTop (gridHeight).reduced (4);
    auto mixer = r.reduced (4, 2);
    sessionGrid->setBounds (grid);

    const int tw = sessionGrid->trackWidth();
    for (int i = 0; i < (int) strips.size(); ++i)
        strips[(size_t) i]->setBounds (mixer.getX() + i * tw, mixer.getY(), tw, mixer.getHeight());
    masterStrip->setBounds (mixer.getX() + (int) strips.size() * tw, mixer.getY(), kSceneColumnWidth, mixer.getHeight());
}
