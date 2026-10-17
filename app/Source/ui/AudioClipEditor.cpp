#include "AudioClipEditor.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "PaletteMenu.h"
#include "PianoRollParts.h"
#include "Waveform.h"

#include <algorithm>
#include <cmath>

namespace ui
{
namespace
{
constexpr int kRow = 22, kGap = 4, kMarginProps = 8;
constexpr int kBraceHeight = 11, kGrab = 5;
constexpr double kSteps[] { 1.0, 0.5, 0.25, 0.125 };
} // namespace

//==============================================================================
// The waveform with the ruler on top
//==============================================================================
class AudioClipEditor::Wave final : public juce::Component
{
public:
    enum class Part
    {
        none,
        startMarker,
        braceStart,
        braceEnd
    };

    explicit Wave (AudioClipEditor& o) : owner (o) { setHelpText (Strings::help::audioWave); }

    juce::Rectangle<int> braceArea() const { return { 0, kRulerHeight - kBraceHeight, getWidth(), kBraceHeight }; }
    juce::Rectangle<int> bodyArea() const { return { 0, kRulerHeight, getWidth(), juce::jmax (0, getHeight() - kRulerHeight) }; }

    Part partAt (juce::Point<int> p) const
    {
        const auto c = owner.clip();
        if (! c.isValid() || p.y >= kRulerHeight)
            return Part::none;
        if (braceArea().contains (p))
        {
            if (std::abs (p.x - owner.beatToX (c.lengthBeats())) <= kGrab)
                return Part::braceEnd;
            if (std::abs (p.x - owner.beatToX (c.loopStart())) <= kGrab)
                return Part::braceStart;
            return Part::none;
        }
        return std::abs (p.x - owner.beatToX (c.startOffset())) <= 7 ? Part::startMarker : Part::none;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        g.fillAll (t.background);
        const auto c = owner.clip();
        if (! c.isValid())
            return;
        paintBody (g, c);
        paintRuler (g, c);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto over = partAt (e.getPosition());
        setMouseCursor (over == Part::none ? juce::MouseCursor::NormalCursor
                                           : (over == Part::startMarker ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::LeftRightResizeCursor));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        changed = false;
        part = partAt (e.getPosition());
        const auto c = owner.clip();
        if (! c.isValid() || part == Part::none)
            return;
        origStart = c.loopStart();
        origEnd = c.lengthBeats();
        owner.frozenView = owner.viewBeats();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto c = owner.clip();
        if (! c.isValid() || part == Part::none)
            return;
        // Shift, Alt and Cmd all switch the snapping off for the gesture
        const bool bypass = e.mods.isShiftDown() || e.mods.isAltDown() || e.mods.isCommandDown();
        const double beat = owner.snapBeat (owner.xToBeat (e.x), bypass);
        const double minBrace = owner.v.snap && ! bypass ? owner.v.step : kMinBraceBeats;
        auto& um = owner.model.undo;
        // One transaction for the whole drag, opened by the first change so that a click that moves nothing leaves no step.
        const auto open = [&] (const juce::String& name) {
            if (! changed)
                um.beginNewTransaction (name);
            changed = true;
        };
        switch (part)
        {
            case Part::startMarker:
            {
                const double value = juce::jlimit (0.0, c.lengthBeats() - kMinBraceBeats, beat);
                if (std::abs (value - c.startOffset()) > 1.0e-12)
                {
                    open (Strings::audioUi::undoStartMarker);
                    c.setStartOffset (value, &um);
                }
                break;
            }
            case Part::braceStart:
            {
                const double value = juce::jlimit (0.0, origEnd - minBrace, beat);
                if (std::abs (value - c.loopStart()) > 1.0e-12)
                {
                    open (Strings::audioUi::undoLoopBrace);
                    c.setLoopStart (value, &um);
                }
                break;
            }
            case Part::braceEnd:
            {
                const double value = juce::jlimit (origStart + minBrace, juce::jmax (origEnd, owner.frozenView), beat);
                if (std::abs (value - c.lengthBeats()) > 1.0e-12)
                {
                    open (Strings::audioUi::undoLoopBrace);
                    c.setLengthBeats (value, &um);
                }
                break;
            }
            case Part::none: break;
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        part = Part::none;
        changed = false;
        owner.frozenView = 0.0;
        repaint();
    }

private:
    void paintRuler (juce::Graphics& g, const ClipRef& c)
    {
        const auto& t = theme();
        g.setColour (t.chrome);
        g.fillRect (0, 0, getWidth(), kRulerHeight);
        g.setColour (t.line);
        g.fillRect (0, kRulerHeight - 1, getWidth(), 1);

        const double bar = owner.model.beatsPerBar();
        const double px = owner.beatToX (1.0) - owner.beatToX (0.0);
        const int labelEvery = px * bar < 34.0 ? (px * bar < 17.0 ? 4 : 2) : 1;
        g.setFont (t.numFont (10.5f));
        for (double b = 0.0; b <= owner.viewBeats() + 1.0e-9; b += 1.0)
        {
            const int x = juce::roundToInt (owner.beatToX (b));
            const double barIndex = b / bar;
            const bool isBar = std::abs (barIndex - std::round (barIndex)) < 1.0e-6;
            if (! isBar && px < 6.0)
                continue;
            g.setColour (t.line);
            g.fillRect (x, isBar ? 2 : kRulerHeight - kBraceHeight - 6, 1, isBar ? kRulerHeight - kBraceHeight - 2 : 5);
            if (isBar && ((int) std::lround (barIndex)) % labelEvery == 0)
            {
                g.setColour (t.dimText);
                g.drawText (juce::String ((int) std::lround (barIndex) + 1), x + 3, 1, 40, 14, juce::Justification::centredLeft, false);
            }
        }

        // the loop brace: from the loop start to the end of the content
        const float sx = (float) owner.beatToX (c.loopStart()), ex = (float) owner.beatToX (c.lengthBeats());
        const auto area = braceArea();
        const bool on = c.loopEnabled();
        const auto colour = on ? t.accent : t.dimText;
        const auto brace = juce::Rectangle<float>::leftTopRightBottom (sx, (float) area.getY() + 1.0f, ex, (float) area.getBottom() - 2.0f);
        if (brace.getWidth() > 0.0f)
        {
            g.setColour (colour.withAlpha (on ? 0.45f : 0.22f));
            g.fillRoundedRectangle (brace, 2.0f);
            g.setColour (colour);
            g.fillRect (brace.withWidth (3.0f));
            g.fillRect (brace.withLeft (brace.getRight() - 3.0f));
        }

        // the start marker: a flag above the brace
        const float px0 = (float) owner.beatToX (c.startOffset());
        juce::Path flag;
        flag.addTriangle (px0 - 5.0f, 15.0f, px0 + 5.0f, 15.0f, px0, 24.0f);
        g.setColour (t.play);
        g.fillPath (flag);
        g.fillRect (px0 - 0.5f, 15.0f, 1.0f, 2.0f);
    }

    void paintBody (juce::Graphics& g, const ClipRef& c)
    {
        const auto& t = theme();
        const auto body = bodyArea().toFloat();
        const double view = owner.viewBeats();

        // beat and grid lines
        const double bar = owner.model.beatsPerBar();
        const double px = owner.beatToX (1.0) - owner.beatToX (0.0);
        const double step = owner.v.step * px >= 8.0 ? owner.v.step : 1.0;
        for (double b = 0.0; b <= view + 1.0e-9; b += step)
        {
            const double barIndex = b / bar;
            const bool isBar = std::abs (barIndex - std::round (barIndex)) < 1.0e-6;
            const bool isBeat = std::abs (b - std::round (b)) < 1.0e-6;
            if (! isBeat && px * step < 8.0)
                continue;
            g.setColour (t.line.withAlpha (isBar ? 0.9f : (isBeat ? 0.45f : 0.2f)));
            g.fillRect ((float) owner.beatToX (b), body.getY(), 1.0f, body.getHeight());
        }
        g.setColour (t.line.withAlpha (0.6f));
        g.fillRect (body.getX(), body.getCentreY(), body.getWidth(), 1.0f);

        const auto fileRef = owner.file();
        const auto source = fileRef.isValid() ? owner.engine.peekAudioSource (c.fileId()) : nullptr;
        if (source == nullptr)
        {
            g.setColour (t.warning);
            g.setFont (t.uiFont (13.0f, 600));
            g.drawText (Strings::audioUi::fileMissing, bodyArea().withTrimmedBottom (14), juce::Justification::centred, true);
            g.setColour (t.dimText);
            g.setFont (t.uiFont (12.0f));
            g.drawText (Strings::audioUi::noFile, bodyArea().withTrimmedTop (14), juce::Justification::centred, true);
            return;
        }

        const juce::Rectangle<float> area ((float) owner.beatToX (0.0), body.getY() + 4.0f, (float) (view * px), body.getHeight() - 8.0f);
        drawAudioWaveform (g, area, *source, c, 0.0, false, view, owner.model.tempo(), t.palette (c.colour()));

        // what the clip does not play: before the start marker the content is dimmed, after the end there is nothing
        const float startX = (float) owner.beatToX (c.startOffset()), endX = (float) owner.beatToX (c.lengthBeats());
        g.setColour (t.background.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (area.getX(), body.getY(), startX, body.getBottom()));
        g.setColour (t.play);
        g.fillRect (startX - 0.5f, body.getY(), 1.0f, body.getHeight());
        g.setColour (c.loopEnabled() ? t.accent : t.dimText);
        g.fillRect (endX - 0.5f, body.getY(), 1.0f, body.getHeight());
        g.fillRect ((float) owner.beatToX (c.loopStart()) - 0.5f, body.getY(), 1.0f, body.getHeight());
    }

    AudioClipEditor& owner;
    Part part = Part::none;
    bool changed = false;
    double origStart = 0.0, origEnd = 0.0;
};

//==============================================================================
// The properties column
//==============================================================================
class AudioClipEditor::Props final : public juce::Component
{
public:
    explicit Props (AudioClipEditor& o) : owner (o), content (o)
    {
        setHelpText (Strings::help::audioProps);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);
    }

    void refresh()
    {
        content.refresh();
        resized();
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds().withTrimmedRight (1));
        const int w = viewport.getMaximumVisibleWidth();
        content.setSize (w, 800);
        content.layout();
        content.setSize (w, juce::jmax (content.contentHeight(), getHeight()));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme().chrome);
        g.setColour (theme().line);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
    }

private:
    class NameField final : public juce::Component
    {
    public:
        explicit NameField (AudioClipEditor& o) : owner (o) { setHelpText (Strings::help::editorName); }

        void paint (juce::Graphics& g) override
        {
            const auto& t = theme();
            auto r = getLocalBounds().toFloat();
            fillCell (g, r, t.panel2, 0.8f);
            strokeCell (g, r, t.line, 1.0f, 0.8f);
            const auto c = owner.clip();
            g.setColour (t.palette (c.isValid() ? c.colour() : 0));
            g.fillRoundedRectangle (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ 14.0f, r.getCentreY() }), 3.0f);
            if (editor != nullptr)
                return;
            g.setColour (t.text);
            g.setFont (t.uiFont (12.5f, 600));
            g.drawText (c.isValid() ? c.name() : juce::String(), getLocalBounds().withTrimmedLeft (28).withTrimmedRight (4), juce::Justification::centredLeft, true);
        }
        void resized() override
        {
            if (editor != nullptr)
                editor->setBounds (getLocalBounds().withTrimmedLeft (26).reduced (1));
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || e.getNumberOfClicks() != 1 || e.x > 26 || e.getDistanceFromDragStart() > 3)
                return;
            const auto id = owner.clipId();
            auto menu = makePaletteMenu (owner.clip().colour(), [safe = juce::Component::SafePointer<AudioClipEditor> (&owner), id] (int index) {
                if (safe != nullptr)
                    safe->actions.setClipColourById (id, index);
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.x <= 26 || editor != nullptr)
                return;
            editor = std::make_unique<InlineEditor>();
            editor->setText (owner.clip().name(), false);
            editor->onCommit = [this] (const juce::String& text) { finish (text); };
            editor->onCancel = [this] { finish ({}); };
            addAndMakeVisible (*editor);
            resized();
            editor->grabKeyboardFocus();
            editor->selectAll();
        }

    private:
        void finish (const juce::String& text)
        {
            auto* dying = editor.release();
            removeChildComponent (dying);
            juce::MessageManager::callAsync ([dying] { delete dying; });
            if (text.isNotEmpty())
                owner.actions.renameClipById (owner.clipId(), text);
            repaint();
        }

        AudioClipEditor& owner;
        std::unique_ptr<InlineEditor> editor;
    };

    class Content final : public juce::Component
    {
    public:
        explicit Content (AudioClipEditor& o)
            : owner (o),
              nameField (o),
              reverseChip (Strings::audioUi::reverse, Strings::help::audioReverse, true),
              loopChip (Strings::audioUi::loop, Strings::help::audioLoop, true),
              snapChip (Strings::audioUi::snap, Strings::help::editorSnap, true),
              gridChip ({}, Strings::help::editorGrid, false)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &nameField, &reverseChip, &loopChip, &snapChip, &gridChip })
                addAndMakeVisible (c);
            for (auto* f : fields())
                addAndMakeVisible (f);

            reverseChip.onClick = [this] { owner.setReverse (! owner.clip().reverse()); };
            loopChip.onClick = [this] { owner.setLoopEnabled (! owner.clip().loopEnabled()); };
            snapChip.onClick = [this] {
                owner.v.snap = ! owner.v.snap;
                refresh();
            };
            gridChip.onClick = [this] { showGridMenu(); };

            const auto setup = [] (NumberField& f, const juce::String& help, double lo, double hi, double interval, int decimals) {
                f.setHelpText (help);
                f.setRange (lo, hi, interval);
                f.setDecimals (decimals);
                f.setDragSensitivity (decimals == 0 ? 0.15 : 0.1);
                f.setWheelStep (interval);
                f.setFontHeight (12.0f);
            };
            setup (gainField, Strings::help::audioGain, -70.0, 24.0, 0.1, 1);
            gainField.setSuffix (" dB");
            setup (transposeField, Strings::help::audioTranspose, -48.0, 48.0, 1.0, 0);
            setup (detuneField, Strings::help::audioDetune, -50.0, 50.0, 1.0, 0);
            setup (loopStartField, Strings::help::editorLoopStart, 0.0, 4096.0, 1.0 / 64.0, 3);
            setup (loopEndField, Strings::help::editorLoopEnd, 1.0 / 64.0, 4096.0, 1.0 / 64.0, 3);
            setup (startField, Strings::help::editorStart, 0.0, 4096.0, 1.0 / 64.0, 3);
            setup (fadeInField, Strings::help::audioFadeIn, 0.0, 64.0, 1.0 / 64.0, 3);
            setup (fadeOutField, Strings::help::audioFadeOut, 0.0, 64.0, 1.0 / 64.0, 3);

            const auto bind = [this] (NumberField& f, const juce::String& undoName, std::function<void (const ClipRef&, double)> apply) {
                f.onGestureStart = [this, undoName] { owner.model.undo.beginNewTransaction (undoName); };
                f.onValueChange = [this, apply] (double value) {
                    if (const auto c = owner.clip(); c.isValid())
                        apply (c, value);
                };
            };
            auto* um = &owner.model.undo;
            bind (gainField, Strings::audioUi::undoGain, [um] (const ClipRef& c, double v) { c.setGainDb (v, um); });
            bind (transposeField, Strings::audioUi::undoTranspose, [um] (const ClipRef& c, double v) { c.setTranspose ((int) std::lround (v), um); });
            bind (detuneField, Strings::audioUi::undoDetune, [um] (const ClipRef& c, double v) { c.setDetune ((int) std::lround (v), um); });
            bind (loopStartField, Strings::audioUi::undoLoopBrace, [um] (const ClipRef& c, double v) {
                c.setLoopStart (juce::jmin (v, c.lengthBeats() - kMinBraceBeats), um);
            });
            bind (loopEndField, Strings::audioUi::undoLoopBrace, [um] (const ClipRef& c, double v) {
                c.setLengthBeats (juce::jmax (v, c.loopStart() + kMinBraceBeats), um);
            });
            bind (startField, Strings::audioUi::undoStartMarker, [um] (const ClipRef& c, double v) {
                c.setStartOffset (juce::jmin (v, c.lengthBeats() - kMinBraceBeats), um);
            });
            bind (fadeInField, Strings::audioUi::undoFadeIn, [um] (const ClipRef& c, double v) { c.setFadeIn (v, um); });
            bind (fadeOutField, Strings::audioUi::undoFadeOut, [um] (const ClipRef& c, double v) { c.setFadeOut (v, um); });
        }

        std::vector<NumberField*> fields()
        {
            return { &gainField, &transposeField, &detuneField, &loopStartField, &loopEndField, &startField, &fadeInField, &fadeOutField };
        }

        void refresh()
        {
            const auto c = owner.clip();
            if (! c.isValid())
                return;
            const auto set = [] (NumberField& f, double value) {
                if (! f.isEditing())
                    f.setValue (value);
            };
            set (gainField, c.gainDb());
            set (transposeField, c.transpose());
            set (detuneField, c.detune());
            set (loopStartField, c.loopStart());
            set (loopEndField, c.lengthBeats());
            set (startField, c.startOffset());
            set (fadeInField, c.fadeIn());
            set (fadeOutField, c.fadeOut());
            reverseChip.setOn (c.reverse());
            loopChip.setOn (c.loopEnabled());
            snapChip.setOn (owner.v.snap);
            gridChip.setText (Strings::audioUi::grid + ": " + owner.gridText());

            const auto f = owner.file();
            info = f.isValid() ? Strings::audioUi::fileInfo (f.name(), f.sampleRate(), f.channels(),
                                                              f.sampleRate() > 0.0 ? (double) f.lengthSamples() / f.sampleRate() : 0.0)
                               : Strings::audioUi::fileMissing;
            const double engineRate = owner.engine.getSampleRate();
            const juce::String warning = f.isValid() && engineRate > 0.0 && std::abs (f.sampleRate() - engineRate) > 0.5
                                             ? Strings::audioUi::rateWarning (f.sampleRate())
                                             : juce::String();
            if (warning != rateWarning)
            {
                rateWarning = warning;
                layoutDirty = true;
            }
            nameField.repaint();
            repaint();
        }

        bool needsLayout() const { return layoutDirty; }

        void layout()
        {
            layoutDirty = false;
            const int w = getWidth() - 2 * kMarginProps;
            int y = kMarginProps;
            nameField.setBounds (kMarginProps, y, w, kRow);
            y += kRow + kGap;
            infoY = y;
            y += 32 + kGap;
            if (rateWarning.isNotEmpty())
            {
                warningY = y;
                y += 30 + kGap;
            }
            else
                warningY = -1;
            labelY = y;
            for (auto* f : { &gainField, &transposeField, &detuneField })
            {
                f->setBounds (kMarginProps + w - 76, y, 76, kRow);
                y += kRow + kGap;
            }
            reverseChip.setBounds (kMarginProps, y, w / 2 - 2, kRow);
            loopChip.setBounds (kMarginProps + w / 2 + 2, y, w - w / 2 - 2, kRow);
            y += kRow + kGap;
            braceY = y;
            for (auto* f : { &loopStartField, &loopEndField, &startField, &fadeInField, &fadeOutField })
            {
                f->setBounds (kMarginProps + w - 76, y, 76, kRow);
                y += kRow + kGap;
            }
            snapChip.setBounds (kMarginProps, y, w / 2 - 2, kRow);
            gridChip.setBounds (kMarginProps + w / 2 + 2, y, w - w / 2 - 2, kRow);
            y += kRow + kGap;
            noteY = y;
            y += 30 + kMarginProps;
            height = y;
        }

        int contentHeight() const { return height; }

        void paint (juce::Graphics& g) override
        {
            const auto& t = theme();
            const int w = getWidth() - 2 * kMarginProps;
            g.setColour (t.dimText);
            g.setFont (t.uiFont (11.5f));
            g.drawFittedText (info, kMarginProps + 2, infoY, w - 4, 32, juce::Justification::topLeft, 2);
            if (warningY >= 0)
            {
                g.setColour (t.warning);
                g.drawFittedText (rateWarning, kMarginProps + 2, warningY, w - 4, 30, juce::Justification::topLeft, 2);
            }
            g.setColour (t.dimText);
            const juce::String* names[3] = { &Strings::audioUi::gain, &Strings::audioUi::transpose, &Strings::audioUi::detune };
            for (int i = 0; i < 3; ++i)
                g.drawText (*names[i], kMarginProps + 2, labelY + i * (kRow + kGap), w - 80, kRow, juce::Justification::centredLeft, true);
            const juce::String* more[5] = { &Strings::audioUi::loopStart, &Strings::audioUi::loopEnd, &Strings::audioUi::startPoint,
                                            &Strings::audioUi::fadeIn, &Strings::audioUi::fadeOut };
            for (int i = 0; i < 5; ++i)
                g.drawText (*more[i], kMarginProps + 2, braceY + i * (kRow + kGap), w - 80, kRow, juce::Justification::centredLeft, true);
            g.setFont (t.uiFont (11.0f));
            g.drawFittedText (Strings::audioUi::warpSoon, kMarginProps + 2, noteY, w - 4, 30, juce::Justification::topLeft, 2);
        }

    private:
        void showGridMenu()
        {
            juce::PopupMenu menu;
            static const char* names[] = { "1/4", "1/8", "1/16", "1/32" };
            for (int i = 0; i < 4; ++i)
                menu.addItem (10 + i, names[i], true, std::abs (owner.v.step - kSteps[i]) < 1.0e-9);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gridChip),
                                [safe = juce::Component::SafePointer<AudioClipEditor> (&owner)] (int result) {
                                    if (safe == nullptr || result < 10)
                                        return;
                                    safe->v.step = kSteps[result - 10];
                                    safe->props->refresh();
                                });
        }

        AudioClipEditor& owner;
        NameField nameField;
        Chip reverseChip, loopChip, snapChip, gridChip;
        NumberField gainField, transposeField, detuneField, loopStartField, loopEndField, startField, fadeInField, fadeOutField;
        juce::String info, rateWarning;
        int infoY = 0, warningY = -1, labelY = 0, braceY = 0, noteY = 0, height = 0;
        bool layoutDirty = true;
    };

    AudioClipEditor& owner;
    Content content;
    juce::Viewport viewport;

    friend class AudioClipEditor;
};

//==============================================================================
AudioClipEditor::AudioClipEditor (AudioEngine& e, ProjectModel& m, ProjectActions& a) : engine (e), model (m), actions (a)
{
    props = std::make_unique<Props> (*this);
    waveView = std::make_unique<Wave> (*this);
    addAndMakeVisible (*props);
    addAndMakeVisible (*waveView);
    model.root.addListener (this);
    startTimerHz (30);
}

AudioClipEditor::~AudioClipEditor()
{
    stopTimer();
    model.root.removeListener (this);
}

juce::Component& AudioClipEditor::wave()
{
    return *waveView;
}

ClipRef AudioClipEditor::clip() const
{
    return currentClip > 0 ? model.findClip (currentClip) : ClipRef();
}

AudioFileRef AudioClipEditor::file() const
{
    const auto c = clip();
    return c.isValid() ? model.findAudioFile (c.fileId()) : AudioFileRef();
}

bool AudioClipEditor::hasClip() const
{
    const auto c = clip();
    return c.isValid() && c.isAudio();
}

void AudioClipEditor::setClip (int64_t id)
{
    if (id == currentClip)
        return;
    currentClip = id;
    dirty = true;
    shownPeaks = -1;
    tick();
}

void AudioClipEditor::tick()
{
    if (! isShowing() && ! dirty)
        return;
    int64_t peaks = 0;
    if (const auto c = clip(); c.isValid())
        if (const auto source = engine.peekAudioSource (c.fileId()))
            peaks = source->peaksAvailable.load (std::memory_order_acquire);
    if (! dirty && peaks == shownPeaks)
        return;
    dirty = false;
    shownPeaks = peaks;
    props->content.refresh();
    if (props->content.needsLayout())
        props->resized();
    waveView->repaint();
}

//==============================================================================
double AudioClipEditor::snapBeat (double beat, bool bypass) const
{
    if (! v.snap || bypass)
        return beat;
    return std::floor (beat / v.step + 0.5) * v.step;
}

juce::String AudioClipEditor::gridText() const
{
    return "1/" + juce::String (juce::roundToInt (4.0 / v.step));
}

double AudioClipEditor::viewBeats() const
{
    if (frozenView > 0.0)
        return frozenView;
    const auto c = clip();
    if (! c.isValid())
        return 4.0;
    double fileBeats = 0.0;
    if (const auto f = file(); f.isValid())
    {
        const double framesPerBeat = f.sampleRate() * 60.0 / juce::jmax (1.0, model.tempo()) * c.pitchRatio();
        fileBeats = juce::jmax (0.0, (double) f.lengthSamples() - c.sampleStart()) / juce::jmax (1.0, framesPerBeat);
    }
    return juce::jmax (1.0, c.lengthBeats(), fileBeats);
}

double AudioClipEditor::beatToX (double beat) const
{
    const double usable = juce::jmax (1, waveView->getWidth() - 2 * kMargin);
    return kMargin + beat / viewBeats() * usable;
}

double AudioClipEditor::xToBeat (double x) const
{
    const double usable = juce::jmax (1, waveView->getWidth() - 2 * kMargin);
    return (x - kMargin) / usable * viewBeats();
}

//==============================================================================
void AudioClipEditor::setGainDb (double db)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoGain);
        c.setGainDb (db, &model.undo);
    }
}

void AudioClipEditor::setTranspose (int semitones)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoTranspose);
        c.setTranspose (semitones, &model.undo);
    }
}

void AudioClipEditor::setDetune (int cents)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoDetune);
        c.setDetune (cents, &model.undo);
    }
}

void AudioClipEditor::setReverse (bool on)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoReverse);
        c.setReverse (on, &model.undo);
    }
}

void AudioClipEditor::setLoopEnabled (bool on)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::undoLoopSwitch);
        c.setLoopEnabled (on, &model.undo);
    }
}

void AudioClipEditor::setFadeIn (double beats)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoFadeIn);
        c.setFadeIn (beats, &model.undo);
    }
}

void AudioClipEditor::setFadeOut (double beats)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoFadeOut);
        c.setFadeOut (beats, &model.undo);
    }
}

//==============================================================================
void AudioClipEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme().background);
    if (! hasClip())
    {
        const auto& t = theme();
        g.setColour (t.text);
        g.setFont (t.uiFont (16.0f, 600));
        g.drawText (Strings::editor::noClip, getLocalBounds().withTrimmedBottom (24), juce::Justification::centred, false);
    }
}

void AudioClipEditor::resized()
{
    auto r = getLocalBounds();
    props->setBounds (r.removeFromLeft (kPropsWidth));
    waveView->setBounds (r);
    dirty = true;
}
} // namespace ui
