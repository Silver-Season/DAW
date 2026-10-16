#include "PrismaEditor.h"

namespace prisma_ui
{

//==============================================================================
PrismaLookAndFeel::PrismaLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
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
}

void Knob::resized()
{
    auto r = getLocalBounds();
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
        addAndMakeVisible (c);
    if (weights.empty())
        weights.assign (items.size(), 1.0f);
    rows.push_back ({ std::move (items), std::move (weights), heightWeight });
}

void Section::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (accentColour);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (title.toUpperCase(), r.reduced (10.0f, 4.0f).removeFromTop (16.0f), juce::Justification::centredLeft);
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
    g.setColour (colours::background);
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
        const float t = (float) f / (float) juce::jmax (1, frames - 1);
        g.setColour (colours::dimText.withAlpha (0.18f));
        g.strokePath (pathFor (f, t * depthX, t * depthY, -1.0f), juce::PathStrokeType (1.0f));
    }

    const float t = (float) active / (float) juce::jmax (1, frames - 1);
    g.setColour ((on ? colours::accent : colours::dimText).withAlpha (on ? 1.0f : 0.6f));
    g.strokePath (pathFor (active, t * depthX, t * depthY, pos), juce::PathStrokeType (2.0f));

    g.setColour (colours::dimText);
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
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, colours::accent.withAlpha (0.7f));
    keyboard.setWantsKeyboardFocus (false); // keys are handled here, for any focused child of the owner
    addAndMakeVisible (keyboard);

    octDown.onClick = [this] { shiftOctave (-1); };
    octUp.onClick = [this] { shiftOctave (1); };
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
    info.setText (juce::MidiMessage::getMidiNoteName (baseNote, true, true, 3) + "\nvel " + juce::String (velocity), juce::dontSendNotification);
    // Shade the keys the computer keyboard reaches.
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, colours::panelEdge);
    keyboard.setKeyPressBaseOctave (baseNote / 12);
    keyboard.setLowestVisibleKey (juce::jlimit (24, 108, baseNote - 24));
}

void KeyboardPanel::shiftOctave (int delta)
{
    const int next = juce::jlimit (12, 108, baseNote + 12 * delta);
    if (next == baseNote)
        return;
    baseNote = next;
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

bool KeyboardPanel::handleKeyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        return false;
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    switch (c)
    {
        case 'z': shiftOctave (-1); return true;
        case 'x': shiftOctave (1); return true;
        case 'c': velocity = juce::jmax (1, velocity - 20); updateLabel(); return true;
        case 'v': velocity = juce::jmin (127, velocity + 20); updateLabel(); return true;
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
            const int note = baseNote + i;
            if (note <= 127)
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
    : AudioProcessorEditor (p), processor (p), keyboard (p.keyboardState)
{
    setLookAndFeel (&lnf);
    auto& s = p.apvts;

    for (int i = 0; i < p.getNumPrograms(); ++i)
        presetBox.addItem (p.getProgramName (i), i + 1);
    presetBox.setSelectedId (p.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] {
        processor.setCurrentProgram (presetBox.getSelectedId() - 1);
    };
    addAndMakeVisible (presetBox);

    voicesLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (voicesLabel);

    oscA = &makeOscSection (0);
    oscB = &makeOscSection (1);

    filter = &make<Section> ("Filter", colours::accent2);
    filter->addRow ({ &make<Toggle> (s, "flt_on", "On"), &make<Choice> (s, "flt_type") }, { 1.0f, 2.0f }, 0.45f);
    filter->addRow ({ &make<Knob> (s, "flt_cutoff", "Cutoff"), &make<Knob> (s, "flt_res", "Reso"), &make<Knob> (s, "flt_drive", "Drive"),
                      &make<Knob> (s, "flt_key", "Key"), &make<Knob> (s, "flt_env", "Env 2") });
    addAndMakeVisible (filter);

    subNoise = &make<Section> ("Sub / Noise", colours::accent2);
    subNoise->addRow ({ &make<Toggle> (s, "sub_on", "Sub"), &make<Choice> (s, "sub_oct"), &make<Knob> (s, "sub_level", "Sub"),
                        &make<Knob> (s, "noise_level", "Noise") },
                      { 1.0f, 1.4f, 1.0f, 1.0f });
    addAndMakeVisible (subNoise);

    for (int e = 1; e <= 3; ++e)
    {
        const juce::String pre = "env" + juce::String (e) + "_";
        auto& sec = make<Section> (e == 1 ? "Amp Env" : "Env " + juce::String (e), e == 1 ? colours::accent : colours::accent2);
        sec.addRow ({ &make<Knob> (s, pre + "a", "A"), &make<Knob> (s, pre + "d", "D"), &make<Knob> (s, pre + "s", "S"), &make<Knob> (s, pre + "r", "R") });
        addAndMakeVisible (sec);
        envs.push_back (&sec);
    }

    for (int l = 1; l <= 2; ++l)
    {
        const juce::String pre = "lfo" + juce::String (l) + "_";
        auto& sec = make<Section> ("LFO " + juce::String (l), colours::accent2);
        sec.addRow ({ &make<Choice> (s, pre + "shape"), &make<Choice> (s, pre + "div") }, {}, 0.55f);
        sec.addRow ({ &make<Knob> (s, pre + "rate", "Rate"), &make<Toggle> (s, pre + "sync", "Sync"), &make<Toggle> (s, pre + "retrig", "Retrig") },
                    { 1.0f, 1.0f, 1.2f });
        addAndMakeVisible (sec);
        lfos.push_back (&sec);
    }

    matrix = &make<Section> ("Mod Matrix");
    for (int row = 0; row < 4; ++row)
    {
        std::vector<juce::Component*> items;
        for (int col = 0; col < 2; ++col)
        {
            const juce::String pre = "mod" + juce::String (row + col * 4 + 1) + "_";
            items.push_back (&make<Choice> (s, pre + "src"));
            items.push_back (&make<Choice> (s, pre + "dst"));
            auto& amt = make<Knob> (s, pre + "amt", "");
            amt.slider.setSliderStyle (juce::Slider::LinearBar);
            amt.slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 20);
            amt.slider.setNumDecimalPlacesToDisplay (2);
            items.push_back (&amt);
        }
        matrix->addRow (items, { 1.1f, 1.4f, 1.0f, 1.1f, 1.4f, 1.0f });
    }
    addAndMakeVisible (matrix);

    master = &make<Section> ("Voice / Master");
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
    auto& s = processor.apvts;
    const juce::String pre = osc == 0 ? "oscA_" : "oscB_";
    auto& sec = make<Section> (osc == 0 ? "Oscillator A" : "Oscillator B");
    sec.addRow ({ &make<Toggle> (s, pre + "on", "On"), &make<Choice> (s, pre + "table") }, { 0.7f, 2.3f }, 0.6f);
    sec.addRow ({ &make<WavetableView> (processor, osc) }, {}, 1.5f);
    sec.addRow ({ &make<Knob> (s, pre + "pos", "WT Pos"), &make<Knob> (s, pre + "level", "Level"), &make<Knob> (s, pre + "pan", "Pan"),
                  &make<Knob> (s, pre + "oct", "Oct"), &make<Knob> (s, pre + "semi", "Semi"), &make<Knob> (s, pre + "fine", "Fine") });
    sec.addRow ({ &make<Knob> (s, pre + "unison", "Unison"), &make<Knob> (s, pre + "detune", "Detune"), &make<Knob> (s, pre + "spread", "Spread"),
                  &make<Knob> (s, pre + "blend", "Blend"), &make<Knob> (s, pre + "phase", "Phase"), &make<Knob> (s, pre + "rand", "Rand") });
    addAndMakeVisible (sec);
    return sec;
}

void PrismaEditor::timerCallback()
{
    voicesLabel.setText ("voices " + juce::String (processor.getNumActiveVoices()), juce::dontSendNotification);
    if (presetBox.getSelectedId() != processor.getCurrentProgram() + 1)
        presetBox.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
}

void PrismaEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    auto header = getLocalBounds().removeFromTop (46).reduced (14, 0);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("PRISMA", header, juce::Justification::centredLeft);
    g.setColour (colours::dimText);
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
    auto top = r.removeFromTop ((int) (h * 0.40f));
    auto mid = r.removeFromTop ((int) (h * 0.25f));
    auto bottom = r;

    const int w = top.getWidth();
    oscA->setBounds (top.removeFromLeft ((int) (w * 0.34f)));
    oscB->setBounds (top.removeFromLeft ((int) (w * 0.34f)));
    filter->setBounds (top.removeFromTop ((int) (top.getHeight() * 0.62f)));
    subNoise->setBounds (top);

    const int sw = mid.getWidth() / 5;
    for (auto* e : envs)
        e->setBounds (mid.removeFromLeft (sw));
    for (size_t i = 0; i < lfos.size(); ++i)
        lfos[i]->setBounds (i + 1 == lfos.size() ? mid : mid.removeFromLeft (sw));

    matrix->setBounds (bottom.removeFromLeft ((int) (bottom.getWidth() * 0.66f)));
    master->setBounds (bottom);
}
