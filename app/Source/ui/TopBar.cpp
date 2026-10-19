#include "TopBar.h"

#include "../Strings.h"
#include "Chrome.h"
#include "ScalePicker.h"
#include "../engine/AudioEngine.h"
#include "../engine/SessionRecorder.h"
#include "TransportMenus.h"
#include "ArrangementView.h"


namespace ui
{
//==============================================================================
class TopBar::IconButton final : public juce::Component
{
public:
    enum class Role
    {
        normal,
        play,
        record,
        warning // lit in the orange of the theme ("Back to arrangement")
    };

    std::function<juce::Path (juce::Rectangle<float>)> icon;
    juce::String text;
    Role role = Role::normal;
    bool on = false;
    bool accentInk = false; // the label in the accent colour (the scale button while a scale is set)
    bool framed = false;    // glass themes: a button of its own (top row), not one of the capsule's row
    std::function<void()> onClick;
    std::function<void()> onPopup; // right mouse button

    IconButton (std::function<juce::Path (juce::Rectangle<float>)> iconFn, const juce::String& label, Role r, const juce::String& help)
        : icon (std::move (iconFn)), text (label), role (r)
    {
        setHelpText (help);
    }

    void setOn (bool v)
    {
        if (on != v)
        {
            on = v;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        juce::Colour fill = t.panel, ink = t.text, border = t.line;
        if (role == Role::record)
            ink = t.record;
        else if (accentInk)
            ink = t.accent;
        if (on)
        {
            const auto accent = role == Role::play ? t.play : (role == Role::record ? t.record : (role == Role::warning ? t.warning : t.accent));
            fill = accent;
            border = accent;
            ink = t.inkOn (accent);
            if ((role == Role::play || role == Role::warning) && t.glow > 0.0f)
                juce::DropShadow (accent.withAlpha (0.6f), juce::roundToInt (14.0f * t.glow), {}).drawForRectangle (g, getLocalBounds().reduced (3));
        }
        else if (isMouseOver() && isEnabled())
            fill = t.panel2;
        if (! isEnabled())
        {
            fill = fill.withMultipliedAlpha (0.5f);
            ink = t.dimText.withAlpha (0.6f);
            border = border.withMultipliedAlpha (0.6f);
        }
        if (t.glass)
        {
            // Inside the transport capsule a button is only its icon until it is lit or under the mouse.
            const float pill = r.getHeight () * 0.5f;
            if (on)
                glass::accent (g, r, fill, pill);
            else if (framed)
            {
                glass::surface (g, r, fill, pill);
                g.setColour (border.withMultipliedAlpha (0.8f));
                g.drawRoundedRectangle (r, pill, 1.0f);
            }
            else if (isMouseOver () && isEnabled ())
            {
                g.setColour (t.panel2.withAlpha (0.8f));
                g.fillRoundedRectangle (r, pill);
            }
        }
        else
        {
            fillCell (g, r, fill);
            strokeCell (g, r, border);
        }

        g.setColour (ink);
        if (icon)
        {
            const float s = text.isEmpty() ? 11.0f : 10.0f;
            const auto box = juce::Rectangle<float> (s, s).withCentre (text.isEmpty() ? r.getCentre() : juce::Point<float> (r.getX() + 15.0f, r.getCentreY()));
            g.fillPath (icon (box));
        }
        if (text.isNotEmpty())
        {
            g.setFont (t.uiFont (getWidth() < 34 ? 10.5f : 12.5f, 500));
            g.drawText (text, getLocalBounds().withTrimmedLeft (icon ? 24 : 0), juce::Justification::centred, false);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! isEnabled() || ! getLocalBounds().contains (e.getPosition()))
            return;
        if (e.mods.isPopupMenu())
        {
            if (onPopup)
                onPopup();
        }
        else if (onClick)
            onClick();
    }
};

//==============================================================================
class TopBar::Lamp final : public juce::Component
{
public:
    Lamp (const juce::String& label, const juce::String& help) : text (label)
    {
        setHelpText (help);
    }

    void setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }

    void setLit (bool v, bool useGreen)
    {
        if (v != lit || useGreen != green)
        {
            lit = v;
            green = useGreen;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto colour = lit ? (green ? t.play : t.accent) : t.line;
        g.setColour (colour);
        const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ 8.0f, (float) getHeight() * 0.5f });
        g.fillEllipse (dot);
        if (lit && t.glow > 0.0f)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillEllipse (dot.expanded (3.0f));
        }
        g.setColour (lit ? t.text : t.dimText);
        g.setFont (t.uiFont (10.5f, 500));
        g.drawText (text, getLocalBounds().withTrimmedLeft (18), juce::Justification::centredLeft, false);
    }

private:
    juce::String text;
    bool lit = false, green = false;
};

//==============================================================================
class TopBar::CpuMeter final : public juce::Component
{
public:
    CpuMeter()
    {
        setHelpText (Strings::help::cpu);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setLoad (float v)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        if (std::abs (v - load) > 0.004f)
        {
            load = v;
            repaint();
        }
    }

    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        g.setColour (t.dimText);
        g.setFont (t.uiFont (10.5f, 500));
        g.drawText (Strings::transport::cpu, getLocalBounds().removeFromLeft (26), juce::Justification::centredLeft, false);
        const auto bar = juce::Rectangle<float> (46.0f, 6.0f).withCentre ({ 31.0f + 23.0f, (float) getHeight() * 0.5f });
        g.setColour (t.panel2);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (load > 0.85f ? t.record : t.play);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * load)), 3.0f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

private:
    float load = 0.0f;
};

//==============================================================================
// A small disk that shows for a moment when the disk could not keep up with playback (AudioEngine::getDiskMisses).
class TopBar::DiskLamp final : public juce::Component
{
public:
    DiskLamp()
    {
        setHelpText (Strings::sessionAudio::diskBehind); // shown in the info bar like every other hint
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto box = juce::Rectangle<float> (14.0f, 14.0f).withCentre (getLocalBounds().toFloat().getCentre());
        juce::Path body; // a floppy disk: a square with a cut corner, a shutter on top and a label below
        body.startNewSubPath (box.getX(), box.getY());
        body.lineTo (box.getRight() - 3.5f, box.getY());
        body.lineTo (box.getRight(), box.getY() + 3.5f);
        body.lineTo (box.getRight(), box.getBottom());
        body.lineTo (box.getX(), box.getBottom());
        body.closeSubPath();
        g.setColour (t.warning);
        g.strokePath (body, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
        g.fillRect (box.getX() + 3.5f, box.getY(), 5.0f, 3.5f);
        g.drawRect (juce::Rectangle<float> (box.getX() + 3.0f, box.getBottom() - 5.5f, box.getWidth() - 6.0f, 5.5f), 1.2f);
    }
};

//==============================================================================
// The A <-> B morph slider; it only looks like the final control (stage 6).
class TopBar::FakeSlider final : public juce::Component
{
public:
    FakeSlider() { setHelpText (Strings::help::morph); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const float alpha = 0.55f;
        auto r = getLocalBounds().toFloat();
        auto letter = [&] (juce::Rectangle<float> box, const juce::String& s) {
            g.setColour (t.panel2.withAlpha (alpha));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (t.line);
            g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (t.dimText);
            g.setFont (t.uiFont (11.0f, 600));
            g.drawText (s, box.toNearestInt(), juce::Justification::centred, false);
        };
        const auto a = r.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, 18.0f);
        const auto b = r.removeFromRight (18.0f).withSizeKeepingCentre (18.0f, 18.0f);
        letter (a, "A");
        letter (b, "B");
        const auto track = r.reduced (7.0f, 0.0f).withSizeKeepingCentre (r.getWidth() - 14.0f, 4.0f);
        g.setColour (t.line);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (t.accent2.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ track.getX() + 6.0f, track.getCentreY() }));
    }
};

//==============================================================================
class TopBar::Tabs final : public juce::Component, public HelpProvider, public juce::DragAndDropTarget, private juce::Timer
{
public:
    Tabs() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    // A Session View clip dragged onto "Arrangement" switches the view after a short hold; the drag then goes on over the
    // timeline, which is a drop target of its own.
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        int64_t track = 0;
        int scene = 0;
        return ArrangementView::parseSessionClip (d.description, track, scene);
    }
    void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
    void itemDragMove (const SourceDetails& d) override
    {
        const bool overArrangement = d.localPosition.x >= getWidth() / 2 && ! arrangement;
        if (overArrangement != hovering)
        {
            hovering = overArrangement;
            hovering ? startTimer (350) : stopTimer();
            repaint();
        }
    }
    void itemDragExit (const SourceDetails&) override { clearHover(); }
    void itemDropped (const SourceDetails&) override { clearHover(); }
    void clearHover()
    {
        hovering = false;
        stopTimer();
        repaint();
    }
    void timerCallback() override
    {
        stopTimer();
        hovering = false;
        if (onSwitch)
            onSwitch (true);
    }

    std::function<void (bool arrangement)> onSwitch;

    void setArrangement (bool a)
    {
        if (a != arrangement)
        {
            arrangement = a;
            repaint();
        }
    }

    juce::String helpAt (juce::Point<int> p) const override
    {
        return p.x < getWidth() / 2 ? Strings::help::tabSession : Strings::help::tabArrangement;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const bool right = e.x >= getWidth() / 2;
        if (right != arrangement && onSwitch)
            onSwitch (right);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (t.panel);
        g.fillRoundedRectangle (r, t.radius);
        auto left = r.removeFromLeft (r.getWidth() * 0.5f);
        const auto drawTab = [&] (juce::Rectangle<float> area, const juce::String& text, bool active) {
            if (active)
            {
                g.setColour (t.panel2);
                g.fillRoundedRectangle (area, t.radius);
                g.setColour (t.accent);
                g.fillRect (area.removeFromBottom (2.0f).reduced (1.0f, 0.0f));
            }
            g.setColour (active ? t.text : t.dimText);
            g.setFont (t.uiFont (12.0f, 500));
            g.drawText (text, area.toNearestInt(), juce::Justification::centred, false);
        };
        drawTab (left, Strings::transport::sessionTab, ! arrangement);
        drawTab (r, Strings::transport::arrangementTab, arrangement);
        if (hovering) // a clip is held over the Arrangement tab
        {
            g.setColour (t.accent.withAlpha (0.25f));
            g.fillRoundedRectangle (r, t.radius);
        }
        strokeCell (g, getLocalBounds().toFloat(), t.line);
    }

private:
    bool arrangement = false, hovering = false;
};

//==============================================================================
TopBar::TopBar (AudioEngine& e, ProjectModel& m) : engine (e), model (m)
{
    setHelpText (Strings::help::logo);

    tempo.setRange (20.0, 999.0, 0.01);
    tempo.setValue (model.tempo());
    tempo.setDecimals (2);
    tempo.setDragSensitivity (0.5);
    tempo.setWheelStep (0.1);
    tempo.setHelpText (Strings::help::tempo);
    tempo.setBoxedInGlass (false);
    tempo.onGestureStart = [this] { model.undo.beginNewTransaction (Strings::undoTempo); };
    tempo.onValueChange = [this] (double) { model.setTempo (tempo.getValue()); };
    addAndMakeVisible (tempo);

    namespace I = icons;
    using Role = IconButton::Role;
    metronome = std::make_unique<IconButton> (I::metronome, juce::String(), Role::normal, Strings::help::metronome);
    play = std::make_unique<IconButton> (I::play, juce::String(), Role::play, Strings::help::play);
    stop = std::make_unique<IconButton> (I::stop, juce::String(), Role::normal, Strings::help::stop);
    record = std::make_unique<IconButton> (I::record, juce::String(), Role::record, Strings::help::record);
    overdub = std::make_unique<IconButton> (nullptr, Strings::recording::ovr, Role::normal, Strings::help::overdub);
    loop = std::make_unique<IconButton> (I::loop, juce::String(), Role::normal, Strings::help::loop);
    punchIn = std::make_unique<IconButton> (nullptr, Strings::arrRec::punchIn, Role::record, Strings::help::punchIn);
    punchOut = std::make_unique<IconButton> (nullptr, Strings::arrRec::punchOut, Role::record, Strings::help::punchOut);
    follow = std::make_unique<IconButton> (I::follow, juce::String(), Role::normal, Strings::help::followPlayhead);
    backToArrangement = std::make_unique<IconButton> (I::backToArrangement, juce::String(), Role::warning, Strings::help::backToArrangement);
    capture = std::make_unique<IconButton> (nullptr, Strings::transport::capture, Role::normal, Strings::help::ribbon);
    version = std::make_unique<IconButton> (nullptr, Strings::transport::versionNone, Role::normal, Strings::help::version);
    appearanceButton =
      std::make_unique<IconButton> (nullptr, Strings::settingsui::tabAppearance, Role::normal, Strings::menu::settings);
    exportButton =
      std::make_unique<IconButton> (nullptr, Strings::menu::exportAudio, Role::normal, Strings::menu::exportAudio);
    appearanceButton->onClick = [this]
    {
        if (onAppearanceClicked)
            onAppearanceClicked ();
    };
    exportButton->onClick = [this]
    {
        if (onExportClicked)
            onExportClicked ();
    };
    addAndMakeVisible (*appearanceButton);
    addAndMakeVisible (*exportButton);
    scaleButton = std::make_unique<IconButton> (nullptr, Strings::transport::scaleOff, Role::normal, Strings::help::scaleButton);
    scaleButton->onClick = [this] {
        showScalePicker (*scaleButton, model.scaleRoot(), model.scaleMode(), [this] (int root, const juce::String& mode) {
            model.undo.beginNewTransaction (Strings::undoProjectScale);
            model.setScale (root, mode);
        });
    };
    loop->onClick = [this] {
        model.undo.beginNewTransaction (Strings::undoArrLoop);
        model.setArrangementLoopOn (! model.arrangementLoopOn());
    };
    punchIn->onClick = [this] { model.setPunchIn (! model.punchIn()); };
    punchOut->onClick = [this] { model.setPunchOut (! model.punchOut()); };
    follow->onClick = [this] { model.setFollowPlayhead (! model.followPlayhead()); };
    backToArrangement->onClick = [this] { engine.backToArrangement(); };
    metronome->onClick = [this] { model.setMetronome (! model.metronome()); };
    metronome->onPopup = [this] {
        makeMetronomeMenu (model).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (metronome.get()));
    };
    record->onClick = [this] {
        if (onRecordClicked)
            onRecordClicked();
    };
    record->onPopup = [this] {
        auto menu = makeRecordMenu (model);
        menu.addSeparator();
        menu.addItem (Strings::arrRec::menuToArrangement, [this] {
            if (onRecordToArrangement)
                onRecordToArrangement();
        });
        menu.addItem (Strings::arrRec::menuToSlot, [this] {
            if (onRecordToSlot)
                onRecordToSlot();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (record.get()));
    };
    overdub->onClick = [this] { model.setOverdub (! model.overdub()); };
    capture->onClick = [this] {
        if (onCaptureClicked)
            onCaptureClicked();
    };
    version->onClick = [this] {
        if (onVersionClicked)
            onVersionClicked();
    };
    play->onClick = [this] {
        if (engine.isPlaying())
            engine.stop();
        else
            engine.play();
    };
    // A second press while stopped brings the song back to its start (and the play start with it).
    stop->onClick = [this] {
        if (engine.isPlaying())
            engine.stop();
        else
        {
            engine.rewindSong();
            model.setArrangementPlayStart (0.0);
        }
    };

    morph = std::make_unique<FakeSlider>();
    spectrum = std::make_unique<Spectrum> (engine);
    addChildComponent (*spectrum);
    tabs = std::make_unique<Tabs>();
    tabs->onSwitch = [this] (bool arrangement) {
        if (onViewSwitched)
            onViewSwitched (arrangement);
    };
    midiLamp = std::make_unique<Lamp> (Strings::transport::midi, Strings::help::midiIn);
    keysLamp = std::make_unique<Lamp> (Strings::transport::keysOn, Strings::help::keysIndicator);
    diskLamp = std::make_unique<DiskLamp>();
    lastDiskMisses = engine.getDiskMisses();
    cpu = std::make_unique<CpuMeter>();
    cpu->onClick = [this] {
        if (onCpuClicked)
            onCpuClicked();
    };

    for (auto* c : std::initializer_list<juce::Component*> { metronome.get(), play.get(), stop.get(), record.get(), overdub.get(), loop.get(), punchIn.get(), punchOut.get(), follow.get(), backToArrangement.get(), capture.get(),
                                                              version.get(), scaleButton.get(), morph.get(), tabs.get(), midiLamp.get(), keysLamp.get(), diskLamp.get(), cpu.get() })
        addAndMakeVisible (c);
    diskLamp->setVisible (false);
    lastMidiCount = engine.getHardwareMidiCount();
    tick();
}

TopBar::~TopBar() = default;

void TopBar::setVersionText (const juce::String& text)
{
    if (version->text != text)
    {
        version->text = text;
        version->repaint();
    }
}

void TopBar::setArrangementShown (bool shown)
{
    tabs->setArrangement (shown);
}

void TopBar::setRecorder (SessionRecorder* r)
{
    recorder = r;
    ribbonVersion = r != nullptr ? r->getCaptureVersion() - 1 : 0;
    updateRibbon();
}

void TopBar::setComputerKeysOn (bool on)
{
    keysLamp->setLit (on, true);
}

void TopBar::setKeysOctave (const juce::String& octave)
{
    keysLamp->setText (Strings::transport::keysOn + " " + octave);
}

void TopBar::tick()
{
    spectrum->tick();
    const bool playing = engine.isPlaying();
    play->setOn (playing);

    if (! tempo.isEditing() && std::abs (tempo.getValue() - model.tempo()) > 0.004)
        tempo.setValue (model.tempo());

    // The song position (the arrangement's); the count-in shows its own run from -4 to 0. 6/8 is three quarter notes long.
    const auto text = formatPosition (engine.isCountingIn() ? engine.getPositionBeats() : engine.getSongPositionBeats(), model.beatsPerBar());
    if (text != positionText)
    {
        positionText = text;
        repaint (positionArea);
    }

    metronome->setOn (model.metronome());
    overdub->setOn (model.overdub());
    loop->setOn (model.arrangementLoopOn());
    punchIn->setOn (model.punchIn());
    punchOut->setOn (model.punchOut());
    follow->setOn (model.followPlayhead());
    {
        const bool inSession = engine.anyTrackInSession();
        backToArrangement->setOn (inSession);
        backToArrangement->setEnabled (inSession);
    }
    {
        // Lit while a recording runs, blinking while one waits for its start or the count-in plays.
        bool recording = false, waiting = engine.isCountingIn();
        if (recorder != nullptr && recorder->arrangement().isActive())
            (recorder->arrangement().isWaiting() ? waiting : recording) = true; // recording onto the timeline (F9)
        if (recorder != nullptr)
            for (const auto& track : model.tracks())
            {
                SessionRecorder::Progress p;
                if (recorder->getProgress (track.id(), p))
                    (p.waiting ? waiting : recording) = true;
                else if (recorder->isTrackRecording (track.id()))
                    waiting = true;
            }
        const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0 || ! engine.getSettings().animations();
        recordLit = recording || (waiting && blink);
        record->setOn (recordLit);
    }
    updateRibbon();

    const auto count = engine.getHardwareMidiCount();
    if (count != lastMidiCount)
    {
        lastMidiCount = count;
        midiFlashUntil = juce::Time::getMillisecondCounter() + 120;
    }
    midiLamp->setLit (juce::Time::getMillisecondCounter() < midiFlashUntil, false);
    const auto label = scaleLabel (model.scaleRoot(), model.scaleMode());
    if (label != scaleText)
    {
        scaleText = label;
        scaleButton->text = label;
        scaleButton->accentInk = model.scaleMode().isNotEmpty();
        scaleButton->repaint();
    }
    {
        // The icon stays for two seconds after the last miss, so a glitch that is over before the eye turns still shows.
        const auto misses = engine.getDiskMisses();
        const auto now = juce::Time::getMillisecondCounter();
        if (misses != lastDiskMisses)
        {
            lastDiskMisses = misses;
            diskShownUntil = now + 2000;
        }
        diskLamp->setVisible (now < diskShownUntil);
    }
    cpu->setLoad ((float) engine.getDeviceManager().getCpuUsage());
    wasPlaying = playing;
}

void TopBar::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    if (t.glass)
    {
        glass::capsule (g, capsuleArea.toFloat ());
        g.setColour (t.line.withAlpha (0.8f));
        for (const int x : separators)
            g.fillRect ((float) x, (float) capsuleArea.getY () + 11.0f, 1.0f, (float) capsuleArea.getHeight () - 22.0f);
    }
    ui::drawLogo (g, logoArea.toFloat());
    g.setColour (t.text);
    g.setFont (t.uiFont (22.0f, 600));
    g.drawText (Strings::labelAppName, juce::Rectangle<int> (64, 10, 105, 34), juce::Justification::centredLeft, false);
    g.setColour (t.dimText);
    g.setFont (t.uiFont (12.5f));
    g.drawFittedText (projectName,
                      juce::Rectangle<int> (185, 10, juce::jmax (60, getWidth () / 2 - 350), 34),
                      juce::Justification::centredLeft,
                      1);

    auto field = [&] (juce::Rectangle<int> area, const juce::String& text) {
        if (! t.glass)
        {
            fillCell (g, area.toFloat(), t.panel);
            strokeCell (g, area.toFloat(), t.line);
        }
        g.setColour (t.text);
        g.setFont (t.numFont (13.0f));
        g.drawText (text, area, juce::Justification::centred, false);
    };
    const int num = model.sigNum(), den = model.sigDen();
    field (signatureArea, juce::String (num) + " / " + juce::String (den));
    field (positionArea, positionText);

    if (! ribbonArea.isEmpty())
        paintRibbon (g);
}

void TopBar::updateRibbon()
{
    if (recorder == nullptr || ribbonStripArea.isEmpty())
        return;
    // 15 frames a second at most, and only when new notes arrived or the strip has moved by a pixel
    if ((++tickCounter & 1) != 0)
        return;
    const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 44100.0;
    const int64_t pixel = (int64_t) std::floor ((double) engine.getEngineSample() / sr * (double) ribbonStripArea.getWidth()
                                                / daw::engine::CaptureBuffer::kWindowSeconds);
    const auto captureVersion = recorder->getCaptureVersion();
    if (captureVersion == ribbonVersion && pixel == ribbonPixel)
        return;
    if (captureVersion != ribbonVersion)
    {
        std::vector<SessionRecorder::NoteMark> marks;
        recorder->getRecentNoteOns (marks);
        markSamples.clear();
        markVelocities.clear();
        markTracks.clear();
        for (const auto& m : marks)
        {
            markSamples.push_back (m.engineSample);
            markVelocities.push_back (m.velocity);
            markTracks.push_back (m.trackId);
        }
    }
    ribbonVersion = captureVersion;
    ribbonPixel = pixel;
    repaint (ribbonStripArea);
}

void TopBar::paintRibbon (juce::Graphics& g)
{
    // The "10 min" capture ribbon: the last ten minutes of playing, newest at the right edge. Every note-on is a
    // stroke in the colour of its track, as tall as it was loud; the red line is "now".
    const auto& t = theme();
    const auto r = ribbonArea.toFloat();
    if (! t.glass)
    {
        fillCell (g, r, t.panel);
        strokeCell (g, r, t.line);
    }
    g.setColour (t.dimText);
    g.setFont (t.uiFont (10.5f, 500));
    g.drawText (Strings::transport::capture10.toUpperCase(), ribbonArea.withTrimmedLeft (9).withWidth (46), juce::Justification::centredLeft, false);

    const auto strip = ribbonStripArea.toFloat();
    g.setColour (t.panel2.withAlpha (0.7f));
    g.fillRoundedRectangle (strip, 3.0f);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (ribbonStripArea);
        const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 44100.0;
        const double now = (double) engine.getEngineSample();
        const double window = daw::engine::CaptureBuffer::kWindowSeconds * sr;
        const float right = strip.getRight() - 2.0f, width = strip.getWidth() - 2.0f;
        const float maxHeight = strip.getHeight() - 4.0f;
        for (size_t i = 0; i < markSamples.size(); ++i)
        {
            const double age = now - (double) markSamples[i];
            if (age < 0.0 || age > window)
                continue;
            const float x = right - (float) (age / window) * width;
            const float h = 3.0f + (float) juce::jlimit (1, 127, markVelocities[i]) / 127.0f * (maxHeight - 3.0f);
            const auto track = markTracks[i] != 0 ? model.findTrack (markTracks[i]) : TrackRef();
            g.setColour (track.isValid() ? t.palette (track.colour()) : t.dimText);
            g.fillRect (x, strip.getCentreY() - h * 0.5f, 1.5f, h);
        }
        g.setColour (t.record);
        g.fillRect (strip.getRight() - 2.0f, strip.getY(), 2.0f, strip.getHeight());
    }
}

void TopBar::resized()
{
    // Two rows. The upper one: the mark and the project name, the Session/Arrangement switch in the middle, the version,
    // export and appearance on the right. The lower one is a capsule holding the transport, left to right in groups,
    // with the lamps and the CPU on its right end; the ribbon and the spectrum take the room left in between.
    constexpr int h = 30, gap = 6, groupGap = 15, cy = 77;
    logoArea = { 17, 7, 38, 38 };
    tabs->setBounds (getWidth () / 2 - 112, 10, 224, 32);
    appearanceButton->setBounds (getWidth () - 14 - 104, 11, 104, 30);
    exportButton->setBounds (appearanceButton->getX () - 8 - 96, 11, 96, 30);
    version->setBounds (exportButton->getX () - 8 - 100, 11, 100, 30);
    for (auto* b : { appearanceButton.get (), exportButton.get (), version.get (), capture.get () })
        b->framed = true;

    capsuleArea = { 10, cy - 21, getWidth () - 20, 42 };
    separators.clear ();
    int x = capsuleArea.getX () + 10;
    auto place = [&] (juce::Component& c, int width)
    {
        c.setBounds (x, cy - h / 2, width, h);
        x += width + gap;
    };
    auto separate = [&]
    {
        x += groupGap - gap;
        separators.push_back (x - groupGap / 2 - 1);
    };
    place (*stop, h);
    place (*play, h);
    place (*record, h);
    place (*overdub, 40);
    separate ();
    positionArea = { x, cy - h / 2, 90, h };
    x += 90 + gap;
    place (tempo, 76);
    signatureArea = { x, cy - h / 2, 54, h };
    x += 54 + gap;
    separate ();
    place (*scaleButton, 100);
    place (*metronome, h);
    place (*loop, h);
    place (*punchIn, 28);
    place (*punchOut, 28);
    place (*follow, h);
    place (*backToArrangement, h);
    separate ();

    auto right = capsuleArea.getRight () - 12;
    auto take = [&] (juce::Component& c, int width)
    {
        right -= width;
        c.setBounds (right, cy - 10, width, 20);
        right -= gap;
    };
    take (*cpu, 80);
    take (*diskLamp, 20);
    take (*keysLamp, 78);
    take (*midiLamp, 48);
    right -= gap;

    ribbonArea = {};
    ribbonStripArea = {};
    capture->setVisible (false);
    morph->setVisible (false);
    spectrum->setVisible (false);
    if (right - x >= 286 + 90 + groupGap)
    {
        ribbonArea = { x, cy - h / 2, 286, h };
        ribbonStripArea = { x + 58, cy - 10, 130, 20 };
        capture->setVisible (true);
        capture->setBounds (x + 198, cy - 13, 82, 26);
        x += 286;
        separate ();
    }
    else if (right - x >= 82 + 90 + groupGap)
    {
        capture->setVisible (true);
        capture->setBounds (x, cy - 13, 82, 26);
        x += 82;
        separate ();
    }
    if (right - x >= 90)
    {
        spectrum->setVisible (true);
        spectrum->setBounds (x, cy - 15, juce::jmin (right - x, 260), 30);
    }
}
} // namespace ui
