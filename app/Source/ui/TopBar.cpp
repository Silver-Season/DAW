#include "TopBar.h"

#include "../Strings.h"
#include "Chrome.h"
#include "ScalePicker.h"
#include "../engine/AudioEngine.h"
#include "../engine/SessionRecorder.h"
#include "TransportMenus.h"


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
        record
    };

    std::function<juce::Path (juce::Rectangle<float>)> icon;
    juce::String text;
    Role role = Role::normal;
    bool on = false;
    bool accentInk = false; // the label in the accent colour (the scale button while a scale is set)
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
            const auto accent = role == Role::play ? t.play : (role == Role::record ? t.record : t.accent);
            fill = accent;
            border = accent;
            ink = t.clipInk;
            if (role == Role::play && t.glow > 0.0f)
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
        fillCell (g, r, fill);
        strokeCell (g, r, border);

        g.setColour (ink);
        if (icon)
        {
            const float s = text.isEmpty() ? 11.0f : 10.0f;
            const auto box = juce::Rectangle<float> (s, s).withCentre (text.isEmpty() ? r.getCentre() : juce::Point<float> (r.getX() + 15.0f, r.getCentreY()));
            g.fillPath (icon (box));
        }
        if (text.isNotEmpty())
        {
            g.setFont (t.uiFont (12.5f, 500));
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
class TopBar::Tabs final : public juce::Component, public HelpProvider
{
public:
    Tabs() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

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
        strokeCell (g, getLocalBounds().toFloat(), t.line);
    }

private:
    bool arrangement = false;
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
    capture = std::make_unique<IconButton> (nullptr, Strings::transport::capture, Role::normal, Strings::help::ribbon);
    version = std::make_unique<IconButton> (nullptr, Strings::transport::versionNone, Role::normal, Strings::help::version);
    scaleButton = std::make_unique<IconButton> (nullptr, Strings::transport::scaleOff, Role::normal, Strings::help::scaleButton);
    scaleButton->onClick = [this] {
        showScalePicker (*scaleButton, model.scaleRoot(), model.scaleMode(), [this] (int root, const juce::String& mode) {
            model.undo.beginNewTransaction (Strings::undoProjectScale);
            model.setScale (root, mode);
        });
    };
    loop->setEnabled (false);
    metronome->onClick = [this] { model.setMetronome (! model.metronome()); };
    metronome->onPopup = [this] {
        makeMetronomeMenu (model).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (metronome.get()));
    };
    record->onClick = [this] {
        if (onRecordClicked)
            onRecordClicked();
    };
    record->onPopup = [this] { makeRecordMenu (model).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (record.get())); };
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
    stop->onClick = [this] { engine.stop(); };

    morph = std::make_unique<FakeSlider>();
    tabs = std::make_unique<Tabs>();
    tabs->onSwitch = [this] (bool arrangement) {
        if (onViewSwitched)
            onViewSwitched (arrangement);
    };
    midiLamp = std::make_unique<Lamp> (Strings::transport::midi, Strings::help::midiIn);
    keysLamp = std::make_unique<Lamp> (Strings::transport::keysOn, Strings::help::keysIndicator);
    cpu = std::make_unique<CpuMeter>();
    cpu->onClick = [this] {
        if (onCpuClicked)
            onCpuClicked();
    };

    for (auto* c : std::initializer_list<juce::Component*> { metronome.get(), play.get(), stop.get(), record.get(), overdub.get(), loop.get(), capture.get(),
                                                              version.get(), scaleButton.get(), morph.get(), tabs.get(), midiLamp.get(), keysLamp.get(), cpu.get() })
        addAndMakeVisible (c);
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
    const bool playing = engine.isPlaying();
    play->setOn (playing);

    if (! tempo.isEditing() && std::abs (tempo.getValue() - model.tempo()) > 0.004)
        tempo.setValue (model.tempo());

    const auto text = formatPosition (engine.getPositionBeats(), model.beatsPerBar()); // 6/8 is three quarter notes long
    if (text != positionText)
    {
        positionText = text;
        repaint (positionArea);
    }

    metronome->setOn (model.metronome());
    overdub->setOn (model.overdub());
    {
        // Lit while a recording runs, blinking while one waits for its start or the count-in plays.
        bool recording = false, waiting = engine.isCountingIn();
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
    cpu->setLoad ((float) engine.getDeviceManager().getCpuUsage());
    wasPlaying = playing;
}

void TopBar::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    ui::drawLogo (g, logoArea.toFloat());
    g.setColour (t.text);
    g.setFont (t.uiFont (15.0f, 600));
    g.drawText (Strings::labelAppName, logoArea.withTrimmedLeft (30).withWidth (80), juce::Justification::centredLeft, false);

    auto field = [&] (juce::Rectangle<int> area, const juce::String& text) {
        fillCell (g, area.toFloat(), t.panel);
        strokeCell (g, area.toFloat(), t.line);
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
    const auto version = recorder->getCaptureVersion();
    if (version == ribbonVersion && pixel == ribbonPixel)
        return;
    if (version != ribbonVersion)
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
    ribbonVersion = version;
    ribbonPixel = pixel;
    repaint (ribbonStripArea);
}

void TopBar::paintRibbon (juce::Graphics& g)
{
    // The "10 min" capture ribbon: the last ten minutes of playing, newest at the right edge. Every note-on is a
    // stroke in the colour of its track, as tall as it was loud; the red line is "now".
    const auto& t = theme();
    const auto r = ribbonArea.toFloat();
    fillCell (g, r, t.panel);
    strokeCell (g, r, t.line);
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
    constexpr int pad = 14, gap = 10, small = 6, h = 30;
    auto r = getLocalBounds().reduced (pad, 0);
    const int cy = getHeight() / 2;
    auto place = [&] (juce::Component& c, int w, int height = 30) {
        c.setBounds (juce::Rectangle<int> (w, height).withCentre ({ r.getX() + w / 2, cy }));
        r.removeFromLeft (w);
    };

    // Left to right
    logoArea = juce::Rectangle<int> (22, 22).withCentre ({ r.getX() + 11, cy });
    r.removeFromLeft (108 + gap);

    tempo.setBounds (juce::Rectangle<int> (80, h).withCentre ({ r.getX() + 40, cy }));
    r.removeFromLeft (80 + small);
    signatureArea = juce::Rectangle<int> (60, h).withCentre ({ r.getX() + 30, cy });
    r.removeFromLeft (60 + small);
    place (*scaleButton, 100);
    r.removeFromLeft (small);
    place (*metronome, h);
    r.removeFromLeft (gap);

    place (*play, h);
    r.removeFromLeft (small);
    place (*stop, h);
    r.removeFromLeft (small);
    place (*record, h);
    r.removeFromLeft (small);
    place (*overdub, 40);
    r.removeFromLeft (small);
    positionArea = juce::Rectangle<int> (90, h).withCentre ({ r.getX() + 45, cy });
    r.removeFromLeft (90 + small);
    place (*loop, h);
    r.removeFromLeft (gap);

    // Right to left: what has to stay visible at the minimum window width
    auto takeRight = [&] (juce::Component& c, int w, int height = 30) {
        c.setBounds (juce::Rectangle<int> (w, height).withCentre ({ r.getRight() - w / 2, cy }));
        r.removeFromRight (w);
    };
    takeRight (*cpu, 80, 20);
    r.removeFromRight (gap);
    takeRight (*keysLamp, 80, 20);
    takeRight (*midiLamp, 52, 20);
    r.removeFromRight (gap);
    takeRight (*version, 96);
    r.removeFromRight (gap);
    takeRight (*tabs, 176, 30);
    r.removeFromRight (gap);

    // Middle: the ribbon and the morph slider appear only if there is room for them.
    const int ribbonWidth = 58 + 130 + 10 + 86 + 6;
    const int morphWidth = 18 + 90 + 18 + 24;
    ribbonArea = {};
    ribbonStripArea = {};
    capture->setVisible (false);
    morph->setVisible (false);
    if (r.getWidth() >= ribbonWidth)
    {
        ribbonArea = juce::Rectangle<int> (ribbonWidth - 4, h).withCentre ({ r.getX() + (ribbonWidth - 4) / 2, cy });
        ribbonStripArea = juce::Rectangle<int> (130, 24).withCentre ({ ribbonArea.getX() + 58 + 65, cy });
        capture->setVisible (true);
        capture->setBounds (juce::Rectangle<int> (82, 24).withCentre ({ ribbonArea.getRight() - 4 - 41, cy }));
        r.removeFromLeft (ribbonWidth - 4 + gap);
        if (r.getWidth() >= morphWidth)
        {
            morph->setVisible (true);
            morph->setBounds (juce::Rectangle<int> (morphWidth, 22).withCentre ({ r.getX() + morphWidth / 2, cy }));
        }
    }
}
} // namespace ui
