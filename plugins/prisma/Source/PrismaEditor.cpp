#include "PrismaEditor.h"

namespace prisma_ui
{

//==============================================================================
PrismaLookAndFeel::PrismaLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::trackColourId, colours::accent);
    setColour (juce::Slider::backgroundColourId, colours::panelEdge);
    setColour (juce::Slider::thumbColourId, colours::text);
    setColour (juce::Label::textColourId, colours::dimText);
    setColour (juce::ComboBox::backgroundColourId, colours::background);
    setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.3f));
    setColour (juce::ToggleButton::textColourId, colours::text);
    setColour (juce::ToggleButton::tickColourId, colours::accent);
    setColour (juce::TextButton::buttonColourId, colours::panel);
    setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.6f));
    setColour (juce::TextButton::textColourOffId, colours::text);
}

void PrismaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float lineW = juce::jmax (2.0f, radius * 0.16f);
    const auto colour = s.findColour (juce::Slider::trackColourId);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - lineW, radius - lineW, 0.0f, start, end, true);
    g.setColour (colours::panelEdge);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters draw from the centre.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - lineW, radius - lineW, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (colour);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (colours::text);
    const float inner = radius - lineW * 2.2f;
    g.drawLine (centre.x + inner * 0.35f * std::sin (angle), centre.y - inner * 0.35f * std::cos (angle),
                centre.x + inner * std::sin (angle), centre.y - inner * std::cos (angle), 2.0f);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption)
    : attachment (state, paramId, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setDoubleClickReturnValue (true, (double) state.getParameter (paramId)->convertFrom0to1 (state.getParameter (paramId)->getDefaultValue()));
    addAndMakeVisible (slider);

    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (11.0f));
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
    value.setJustificationType (juce::Justification::centred);
    value.setFont (juce::FontOptions (10.0f));
    value.setInterceptsMouseClicks (false, false);
    value.setAlpha (0.8f);
    addChildComponent (value);
    const auto update = [this] { value.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification); };
    slider.onValueChange = update;
    update();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    // a tall knob shows its value under the caption, so the patch reads without hovering
    const bool withValue = label.getText().isNotEmpty() && r.getHeight() >= 64 && slider.isRotary();
    value.setVisible (withValue);
    if (withValue)
        value.setBounds (r.removeFromBottom (12));
    if (label.getText().isNotEmpty())
        label.setBounds (r.removeFromBottom (14));
    slider.setBounds (r);
}

Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        box.addItemList (choice->choices, 1);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, box);
    addAndMakeVisible (box);
}

Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text)
    : attachment (state, paramId, button)
{
    button.setButtonText (text);
    addAndMakeVisible (button);
}

//==============================================================================
Section::Section (juce::String t, juce::Colour c) : title (std::move (t)), accentColour (c) {}

void Section::addRow (std::vector<juce::Component*> items, std::vector<float> weights, float heightWeight)
{
    for (auto* c : items)
    {
        addAndMakeVisible (c);
        if (auto* k = dynamic_cast<Knob*> (c)) // the knobs of a section in its colour
            k->slider.setColour (juce::Slider::trackColourId, accentColour);
    }
    if (weights.empty())
        weights.assign (items.size(), 1.0f);
    rows.push_back ({ std::move (items), std::move (weights), heightWeight });
}

void Section::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (findColour (juce::PopupMenu::backgroundColourId));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (0.8f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    // the title: a dot in the section's colour, then the name in the text colour
    const auto titleArea = r.reduced (10.0f, 4.0f).removeFromTop (16.0f);
    g.setColour (accentColour);
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ titleArea.getX() + 4.5f, titleArea.getCentreY() }));
    g.setColour (findColour (juce::ComboBox::textColourId));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (title.toUpperCase(), titleArea.withTrimmedLeft (16.0f), juce::Justification::centredLeft);
}

//==============================================================================
ParamView::ParamView (juce::AudioProcessorValueTreeState& s, Kind k, juce::String pre, juce::Colour c)
    : state (s), kind (k), prefix (std::move (pre)), colour (c)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (15);
}

float ParamView::norm (const juce::String& id) const
{
    if (auto* p = state.getParameter (prefix + id))
        return p->getValue();
    return 0.0f;
}

void ParamView::timerCallback()
{
    std::vector<float> now;
    for (const auto* id : { "a", "d", "s", "r", "shape", "rate", "cutoff", "res", "type" })
        now.push_back (norm (id));
    if (now != last)
    {
        last = std::move (now);
        repaint();
    }
}

void ParamView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f, 3.0f);
    g.setColour (findColour (juce::ResizableWindow::backgroundColourId).withAlpha (0.45f));
    g.fillRoundedRectangle (r, 6.0f);
    r = r.reduced (6.0f, 6.0f);
    juce::Path line;
    if (kind == Kind::envelope)
    {
        // attack, decay and release share the width by their (normalised) times; sustain is a level
        const float a = 0.08f + norm ("a"), d = 0.08f + norm ("d"), rel = 0.08f + norm ("r"), hold = 0.35f;
        const float total = a + d + hold + rel, sustain = norm ("s");
        const auto xAt = [&] (float f) { return r.getX() + r.getWidth() * f / total; };
        const float top = r.getY(), bottom = r.getBottom(), sy = bottom - (bottom - top) * sustain;
        line.startNewSubPath (r.getX(), bottom);
        line.quadraticTo (xAt (a * 0.5f), top + (bottom - top) * 0.15f, xAt (a), top);
        line.quadraticTo (xAt (a + d * 0.3f), sy, xAt (a + d), sy);
        line.lineTo (xAt (a + d + hold), sy);
        line.quadraticTo (xAt (a + d + hold + rel * 0.3f), bottom, xAt (total), bottom);
    }
    else if (kind == Kind::lfo)
    {
        const int shape = juce::roundToInt (norm ("shape") * 5.0f);
        const int cycles = 2;
        for (int i = 0; i <= 160; ++i)
        {
            const float ph = (float) i / 160.0f * (float) cycles;
            const float f = ph - std::floor (ph);
            float v = 0.0f;
            switch (shape)
            {
                case 0: v = std::sin (f * juce::MathConstants<float>::twoPi); break;
                case 1: v = f < 0.5f ? f * 4.0f - 1.0f : 3.0f - f * 4.0f; break;
                case 2: v = f * 2.0f - 1.0f; break;
                case 3: v = 1.0f - f * 2.0f; break;
                case 4: v = f < 0.5f ? 1.0f : -1.0f; break;
                default: v = std::sin ((float) (int) (ph * 6.0f) * 2.3f) * 0.9f; break; // sample and hold: steps
            }
            const float x = r.getX() + r.getWidth() * (float) i / 160.0f, y = r.getCentreY() - v * r.getHeight() * 0.42f;
            i == 0 ? line.startNewSubPath (x, y) : line.lineTo (x, y);
        }
    }
    else
    {
        // an approximate response: flat, a resonant bump at the cutoff, then a slope (mirrored for high-pass)
        const int type = juce::roundToInt (norm ("type") * 5.0f);
        const float cut = 0.12f + norm ("cutoff") * 0.8f, res = norm ("res");
        for (int i = 0; i <= 160; ++i)
        {
            const float x01 = (float) i / 160.0f;
            const float dx = (type == 2 || type == 3 ? cut - x01 : x01 - cut) * (type == 1 || type == 3 ? 9.0f : 5.5f);
            float level = dx < 0.0f ? 0.0f : -dx * dx;
            level += res * 0.6f * std::exp (-std::pow ((x01 - cut) * 18.0f, 2.0f));
            if (type == 4)
                level = -std::pow ((x01 - cut) * 6.0f, 2.0f) + res * 0.6f;
            if (type == 5)
                level = -0.9f * std::exp (-std::pow ((x01 - cut) * 22.0f, 2.0f));
            const float y = r.getY() + r.getHeight() * 0.35f - level * r.getHeight() * 0.5f;
            const float px = r.getX() + r.getWidth() * x01, py = juce::jlimit (r.getY(), r.getBottom(), y);
            i == 0 ? line.startNewSubPath (px, py) : line.lineTo (px, py);
        }
    }
    if (kind != Kind::lfo)
    {
        auto area = line;
        area.lineTo (r.getRight(), r.getBottom());
        area.lineTo (r.getX(), r.getBottom());
        area.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.35f), 0.0f, r.getY(), colour.withAlpha (0.04f), 0.0f, r.getBottom(), false));
        g.fillPath (area);
    }
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Section::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (18);
    float totalH = 0.0f;
    for (auto& row : rows)
        totalH += row.height;

    float y = (float) r.getY();
    for (auto& row : rows)
    {
        const float h = (float) r.getHeight() * row.height / juce::jmax (0.001f, totalH);
        float totalW = 0.0f;
        for (auto w : row.weights)
            totalW += w;
        float x = (float) r.getX();
        for (size_t i = 0; i < row.items.size(); ++i)
        {
            const float w = (float) r.getWidth() * row.weights[i] / juce::jmax (0.001f, totalW);
            if (row.items[i] != nullptr)
                row.items[i]->setBounds (juce::Rectangle<float> (x, y, w, h).toNearestInt());
            x += w;
        }
        y += h;
    }
}

//==============================================================================
WavetableView::WavetableView (PrismaProcessor& p, int osc) : processor (p)
{
    colour = osc == 0 ? juce::Colour (0xff9855ef) : juce::Colour (0xff25dca0);
    const juce::String pre = osc == 0 ? "oscA_" : "oscB_";
    tableParam = p.apvts.getRawParameterValue (pre + "table");
    posParam = p.apvts.getRawParameterValue (pre + "pos");
    onParam = p.apvts.getRawParameterValue (pre + "on");
    startTimerHz (30);
}

void WavetableView::timerCallback()
{
    if (! juce::exactlyEqual (tableParam->load(), lastTable) || ! juce::exactlyEqual (posParam->load(), lastPos)
        || ! juce::exactlyEqual (onParam->load(), lastOn))
    {
        lastTable = tableParam->load();
        lastPos = posParam->load();
        lastOn = onParam->load();
        repaint();
    }
}

void WavetableView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (findColour (juce::ResizableWindow::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);

    const auto* table = processor.getTable ((int) std::lround (tableParam->load()));
    if (table == nullptr)
        return;

    const bool on = onParam->load() > 0.5f;
    const int frames = table->numFrames();
    const float pos = posParam->load();
    const int active = (int) std::lround (pos * (float) (frames - 1));
    const float depthX = r.getWidth() * 0.16f, depthY = r.getHeight() * 0.22f;
    const auto area = r.reduced (8.0f).withTrimmedRight (depthX).withTrimmedTop (depthY);
    constexpr int points = 192;
    constexpr int displayMip = 3;

    auto pathFor = [&] (int frame, float offX, float offY, float morph) {
        juce::Path p;
        for (int i = 0; i <= points; ++i)
        {
            const double phase = (double) i / (points + 1);
            const float v = morph >= 0.0f ? table->sample (morph, phase, displayMip)
                                          : table->data (frame, displayMip)[(int) (phase * daw::dsp::Wavetable::kFrameSize)];
            const float px = area.getX() + offX + area.getWidth() * (float) i / points;
            const float py = area.getCentreY() - offY - v * area.getHeight() * 0.45f;
            if (i == 0)
                p.startNewSubPath (px, py);
            else
                p.lineTo (px, py);
        }
        return p;
    };

    for (int f = frames - 1; f >= 0; f -= 2)
    {
        // the frames behind in the oscillator's colour, fading with depth
        const float t = (float) f / (float) juce::jmax (1, frames - 1);
        g.setColour ((on ? colour : findColour (juce::Label::textColourId)).withAlpha (on ? 0.12f + 0.22f * (1.0f - t) : 0.15f));
        g.strokePath (pathFor (f, t * depthX, t * depthY, -1.0f), juce::PathStrokeType (1.0f));
    }

    const float t = (float) active / (float) juce::jmax (1, frames - 1);
    g.setColour ((on ? colour : findColour (juce::Label::textColourId)).withAlpha (on ? 1.0f : 0.6f));
    g.strokePath (pathFor (active, t * depthX, t * depthY, pos), juce::PathStrokeType (2.0f));

    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (juce::String (table->name()) + "  " + juce::String (active + 1) + "/" + juce::String (frames),
                r.reduced (6.0f, 3.0f), juce::Justification::topLeft);
}

//==============================================================================
KeyboardPanel::KeyboardPanel (juce::MidiKeyboardState& s)
    : state (s), keyboard (s, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    heldNote.fill (-1);
    keyboard.setAvailableRange (24, 108);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,
                        findColour (juce::Slider::trackColourId).withAlpha (0.7f));
    keyboard.setWantsKeyboardFocus (false); // keys are handled here, for any focused child of the owner
    addAndMakeVisible (keyboard);

    octDown.onClick = [this] { onOctaveButton ? onOctaveButton (-1) : shiftOctave (-1); };
    octUp.onClick = [this] { onOctaveButton ? onOctaveButton (1) : shiftOctave (1); };
    octDown.setTooltip ("Octave down (Z)");
    octUp.setTooltip ("Octave up (X)");
    for (auto* b : { &octDown, &octUp })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    info.setJustificationType (juce::Justification::centred);
    info.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (info);
    updateLabel();
}

KeyboardPanel::~KeyboardPanel()
{
    releaseAll();
}

void KeyboardPanel::updateLabel()
{
    info.setText (captionFormatter ? captionFormatter (baseNote, velocity)
                                   : juce::MidiMessage::getMidiNoteName (baseNote, true, true, 3) + "\nvel " + juce::String (velocity),
                  juce::dontSendNotification);
    // Shade the keys the computer keyboard reaches.
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId,
                        findColour (juce::ComboBox::outlineColourId));
    keyboard.setKeyPressBaseOctave (baseNote / 12);
    keyboard.setLowestVisibleKey (juce::jlimit (24, 108, baseNote - 24));
}

void KeyboardPanel::shiftOctave (int delta)
{
    const int next = juce::jlimit (12, 108, baseNote + 12 * delta);
    if (next != baseNote)
    {
        baseNote = next;
        updateLabel();
    }
    if (onChanged)
        onChanged (false);
}

void KeyboardPanel::showState (int newBaseNote, int newVelocity)
{
    if (newBaseNote == baseNote && newVelocity == velocity)
        return;
    baseNote = newBaseNote;
    velocity = newVelocity;
    updateLabel();
}

void KeyboardPanel::releaseAll()
{
    for (auto& n : heldNote)
        if (n >= 0)
        {
            state.noteOff (1, n, 0.0f);
            n = -1;
        }
}

void KeyboardPanel::releaseKeysNotDown()
{
    for (int i = 0; kKeys[i] != 0; ++i)
        if (auto& held = heldNote[(size_t) i]; held >= 0 && ! juce::KeyPress::isKeyCurrentlyDown (kKeys[i]))
        {
            state.noteOff (1, held, 0.0f);
            held = -1;
        }
}

int KeyboardPanel::whiteKeyDegree (int i)
{
    static constexpr int degrees[17] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9 };
    return i >= 0 && i < 17 ? degrees[i] : -1;
}

juce::juce_wchar KeyboardPanel::keyLetter (const juce::KeyPress& key)
{
    // The key code, not the typed character: with a Russian (or any non-Latin) layout the Z key types "я", but its
    // key code is still 'Z'. The note keys are read by key code too (isKeyCurrentlyDown), so all of them agree.
    const int code = key.getKeyCode();
    return code > 0 && code < 128 ? juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code) : 0;
}

bool KeyboardPanel::handleKeyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        return false;
    const auto c = keyLetter (key);
    switch (c)
    {
        case 'z': shiftOctave (-1); return true;
        case 'x': shiftOctave (1); return true;
        case 'c': velocity = juce::jmax (1, velocity - 20); updateLabel(); if (onChanged) onChanged (true); return true;
        case 'v': velocity = juce::jmin (127, velocity + 20); updateLabel(); if (onChanged) onChanged (true); return true;
        default: break;
    }
    return juce::String (kKeys).containsChar (c); // swallow note keys so they don't beep
}

bool KeyboardPanel::handleKeyStateChanged()
{
    bool used = false;
    const bool modifierDown = juce::ModifierKeys::currentModifiers.isCommandDown() || juce::ModifierKeys::currentModifiers.isCtrlDown();
    for (int i = 0; kKeys[i] != 0; ++i)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown (kKeys[i]);
        auto& held = heldNote[(size_t) i];
        if (down && held < 0 && ! modifierDown)
        {
            int note = baseNote + i;
            if (noteForKey)
                if (const int mapped = noteForKey (i, baseNote); mapped != kDefaultNote)
                    note = mapped;
            if (note >= 0 && note <= 127)
            {
                state.noteOn (1, note, (float) velocity / 127.0f);
                held = note;
                used = true;
            }
        }
        else if (! down && held >= 0)
        {
            state.noteOff (1, held, 0.0f); // the note that was played, even if the octave changed since
            held = -1;
            used = true;
        }
    }
    return used;
}

void KeyboardPanel::resized()
{
    auto r = getLocalBounds();
    auto side = r.removeFromLeft (64).reduced (3);
    octUp.setBounds (side.removeFromTop (side.getHeight() / 3));
    octDown.setBounds (side.removeFromBottom (side.getHeight() / 2));
    info.setBounds (side);
    keyboard.setKeyWidth ((float) r.getWidth() / 36.0f); // five octaves visible around the playing range
    keyboard.setBounds (r);
}

} // namespace prisma_ui

//==============================================================================
using namespace prisma_ui;

PrismaEditor::PrismaEditor (PrismaProcessor& p)
  : AudioProcessorEditor (p)
  , synthProcessor (p)
  , keyboard (p.keyboardState)
{
    setLookAndFeel (&lnf);
    auto& s = p.apvts;

    for (int i = 0; i < p.getNumPrograms(); ++i)
        presetBox.addItem (p.getProgramName (i), i + 1);
    presetBox.setSelectedId (p.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { synthProcessor.setCurrentProgram (presetBox.getSelectedId () - 1); };
    addAndMakeVisible (presetBox);

    voicesLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (voicesLabel);

    oscA = &makeOscSection (0);
    oscB = &makeOscSection (1);

    filter = &make<Section> ("Filter", juce::Colour (0xffff4d6d));
    filter->addRow ({ &make<Toggle> (s, "flt_on", "On"), &make<Choice> (s, "flt_type") }, { 1.0f, 2.0f }, 0.45f);
    filter->addRow ({ &make<ParamView> (s, ParamView::Kind::filter, "flt_", juce::Colour (0xffff4d6d)) }, {}, 0.8f);
    filter->addRow ({ &make<Knob> (s, "flt_cutoff", "Cutoff"), &make<Knob> (s, "flt_res", "Reso"), &make<Knob> (s, "flt_drive", "Drive"),
                      &make<Knob> (s, "flt_key", "Key"), &make<Knob> (s, "flt_env", "Env 2") });
    addAndMakeVisible (filter);

    subNoise = &make<Section> ("Sub / Noise", juce::Colour (0xfff4cd48));
    subNoise->addRow ({ &make<Toggle> (s, "sub_on", "Sub"), &make<Choice> (s, "sub_oct"), &make<Knob> (s, "sub_level", "Sub"),
                        &make<Knob> (s, "noise_level", "Noise") },
                      { 1.0f, 1.4f, 1.0f, 1.0f });
    addAndMakeVisible (subNoise);

    for (int e = 1; e <= 3; ++e)
    {
        const juce::String pre = "env" + juce::String (e) + "_";
        const juce::Colour envColour[] { juce::Colour (0xff25dca0), juce::Colour (0xff16bddd), juce::Colour (0xff9855ef) };
        auto& sec = make<Section> (e == 1 ? "Amp Env" : "Env " + juce::String (e), envColour[e - 1]);
        sec.addRow ({ &make<ParamView> (s, ParamView::Kind::envelope, pre, envColour[e - 1]) }, {}, 1.1f);
        sec.addRow ({ &make<Knob> (s, pre + "a", "A"), &make<Knob> (s, pre + "d", "D"), &make<Knob> (s, pre + "s", "S"), &make<Knob> (s, pre + "r", "R") });
        addAndMakeVisible (sec);
        envs.push_back (&sec);
    }

    for (int l = 1; l <= 2; ++l)
    {
        const juce::String pre = "lfo" + juce::String (l) + "_";
        const auto lfoColour = l == 1 ? juce::Colour (0xff448aff) : juce::Colour (0xffff9c49);
        auto& sec = make<Section> ("LFO " + juce::String (l), lfoColour);
        sec.addRow ({ &make<Choice> (s, pre + "shape"), &make<Choice> (s, pre + "div") }, {}, 0.55f);
        sec.addRow ({ &make<ParamView> (s, ParamView::Kind::lfo, pre, lfoColour) }, {}, 0.8f);
        sec.addRow ({ &make<Knob> (s, pre + "rate", "Rate"), &make<Toggle> (s, pre + "sync", "Sync"), &make<Toggle> (s, pre + "retrig", "Retrig") },
                    { 1.0f, 1.0f, 1.2f });
        addAndMakeVisible (sec);
        lfos.push_back (&sec);
    }

    matrix = &make<Section> ("Mod Matrix", juce::Colour (0xff16bddd));
    for (int row = 0; row < 4; ++row)
    {
        std::vector<juce::Component*> items;
        for (int col = 0; col < 2; ++col)
        {
            const juce::String pre = "mod" + juce::String (row + col * 4 + 1) + "_";
            items.push_back (&make<Choice> (s, pre + "src"));
            items.push_back (&make<Choice> (s, pre + "dst"));
            auto& amt = make<Knob> (s, pre + "amt", "");
            amt.slider.setSliderStyle (juce::Slider::LinearHorizontal); // a slider from the centre, the amount on its left
            amt.slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 20);
            amt.slider.setNumDecimalPlacesToDisplay (2);
            items.push_back (&amt);
        }
        matrix->addRow (items, { 1.1f, 1.4f, 1.0f, 1.1f, 1.4f, 1.0f });
    }
    addAndMakeVisible (matrix);

    master = &make<Section> ("Voice / Master", juce::Colour (0xffeb48cc));
    master->addRow ({ &make<Knob> (s, "master_gain", "Gain"), &make<Knob> (s, "poly", "Voices"), &make<Knob> (s, "glide", "Glide"),
                      &make<Knob> (s, "bend", "Bend"), &make<Knob> (s, "vel_sens", "Velocity") });
    master->addRow ({ &make<Toggle> (s, "mono", "Mono"), &make<Toggle> (s, "legato", "Legato") }, {}, 0.5f);
    addAndMakeVisible (master);

    addAndMakeVisible (keyboard);
    setWantsKeyboardFocus (true);

    setResizable (true, true);
    setResizeLimits (1000, 680, 1800, 1200);
    setSize (1200, 780);
    startTimerHz (10);
}

PrismaEditor::~PrismaEditor()
{
    setLookAndFeel (nullptr);
}

Section& PrismaEditor::makeOscSection (int osc)
{
    auto& s = synthProcessor.apvts;
    const juce::String pre = osc == 0 ? "oscA_" : "oscB_";
    auto& sec = make<Section> (osc == 0 ? "Oscillator A" : "Oscillator B", osc == 0 ? juce::Colour (0xff9855ef) : juce::Colour (0xff25dca0));
    sec.addRow ({ &make<Toggle> (s, pre + "on", "On"), &make<Choice> (s, pre + "table") }, { 0.7f, 2.3f }, 0.6f);
    sec.addRow ({ &make<WavetableView> (synthProcessor, osc) }, {}, 1.5f);
    sec.addRow ({ &make<Knob> (s, pre + "pos", "WT Pos"), &make<Knob> (s, pre + "level", "Level"), &make<Knob> (s, pre + "pan", "Pan"),
                  &make<Knob> (s, pre + "oct", "Oct"), &make<Knob> (s, pre + "semi", "Semi"), &make<Knob> (s, pre + "fine", "Fine") });
    sec.addRow ({ &make<Knob> (s, pre + "unison", "Unison"), &make<Knob> (s, pre + "detune", "Detune"), &make<Knob> (s, pre + "spread", "Spread"),
                  &make<Knob> (s, pre + "blend", "Blend"), &make<Knob> (s, pre + "phase", "Phase"), &make<Knob> (s, pre + "rand", "Rand") });
    addAndMakeVisible (sec);
    return sec;
}

void PrismaEditor::timerCallback()
{
    voicesLabel.setText ("voices " + juce::String (synthProcessor.getNumActiveVoices ()), juce::dontSendNotification);
    if (presetBox.getSelectedId () != synthProcessor.getCurrentProgram () + 1)
        presetBox.setSelectedId (synthProcessor.getCurrentProgram () + 1, juce::dontSendNotification);
}

void PrismaEditor::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));
    auto header = getLocalBounds().removeFromTop (46).reduced (14, 0);
    g.setColour (findColour (juce::TextEditor::textColourId));
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("PRISMA", header, juce::Justification::centredLeft);
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("wavetable synthesizer", header.withTrimmedLeft (118), juce::Justification::centredLeft);
}

void PrismaEditor::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto header = r.removeFromTop (40);
    voicesLabel.setBounds (header.removeFromRight (90));
    presetBox.setBounds (header.removeFromRight (260).reduced (4, 6));

    keyboard.setBounds (r.removeFromBottom (70));

    const int h = r.getHeight();
    auto top = r.removeFromTop ((int)((float)h * 0.40f));
    auto mid = r.removeFromTop ((int)((float)h * 0.32f));
    auto bottom = r;

    const int w = top.getWidth();
    oscA->setBounds (top.removeFromLeft ((int)((float)w * 0.34f)));
    oscB->setBounds (top.removeFromLeft ((int)((float)w * 0.34f)));
    filter->setBounds (top.removeFromTop ((int)((float)top.getHeight () * 0.62f)));
    subNoise->setBounds (top);

    const int sw = mid.getWidth() / 5;
    for (auto* e : envs)
        e->setBounds (mid.removeFromLeft (sw));
    for (size_t i = 0; i < lfos.size(); ++i)
        lfos[i]->setBounds (i + 1 == lfos.size() ? mid : mid.removeFromLeft (sw));

    matrix->setBounds (bottom.removeFromLeft ((int)((float)bottom.getWidth () * 0.66f)));
    master->setBounds (bottom);
}
