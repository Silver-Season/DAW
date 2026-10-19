#include "SettingsWindow.h"

#include "../Settings.h"
#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "../engine/PluginFolders.h"
#include "../library/UserLibrary.h"
#include "Dialogs.h"
#include "Theme.h"
#include "UiCommon.h"

namespace ui
{
namespace
{
constexpr int kRowHeight = 28;
constexpr int kLabelWidth = 120;

bool useNativeChooser()
{
#if JUCE_LINUX
    return false; // the JUCE chooser works without zenity
#else
    return true;
#endif
}

// A column of fixed-height rows inside a viewport.
class RowStack final : public juce::Component
{
public:
    void clear()
    {
        rows.clear();
        removeAllChildren();
        layout();
    }
    void add (std::unique_ptr<juce::Component> c)
    {
        addAndMakeVisible (*c);
        rows.push_back (std::move (c));
        layout();
    }
    int totalHeight() const { return (int) rows.size() * kRowHeight; }
    void layout()
    {
        int y = 0;
        for (auto& r : rows)
        {
            r->setBounds (0, y, getWidth(), kRowHeight);
            y += kRowHeight;
        }
    }
    void resized() override { layout(); }
    int numRows() const { return (int) rows.size(); }

private:
    std::vector<std::unique_ptr<juce::Component>> rows;
};

// A scrolling list of rows: the stack in a viewport, sized by the owner.
class RowList final : public juce::Component
{
public:
    RowList()
    {
        viewport.setViewedComponent (&stack, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }
    ~RowList() override { viewport.setViewedComponent (nullptr, false); }
    RowStack& rows() { return stack; }
    void update()
    {
        stack.setSize (juce::jmax (10, getWidth() - viewport.getScrollBarThickness()), juce::jmax (stack.totalHeight(), 1));
        stack.layout();
    }
    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        update();
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme().line);
        g.drawRect (getLocalBounds());
    }

private:
    juce::Viewport viewport;
    RowStack stack;
};

class Page : public juce::Component
{
public:
    virtual void refresh() {}
};

void styleCombo (juce::ComboBox& box)
{
    box.setColour (juce::ComboBox::backgroundColourId, theme().panel);
    box.setColour (juce::ComboBox::outlineColourId, theme().line);
    box.setColour (juce::ComboBox::textColourId, theme().text);
}
} // namespace

//==============================================================================
// Sound
class SoundPage final : public Page, private juce::Timer, private juce::ChangeListener
{
public:
    SoundPage (AudioEngine& e, Settings& s) : engine (e), settings (s)
    {
        for (auto* l : { &typeLabel, &inLabel, &outLabel, &rateLabel, &bufLabel })
            addAndMakeVisible (*l);
        typeLabel.setText (Strings::settingsui::driver, juce::dontSendNotification);
        inLabel.setText (Strings::settingsui::input, juce::dontSendNotification);
        outLabel.setText (Strings::settingsui::output, juce::dontSendNotification);
        rateLabel.setText (Strings::settingsui::sampleRate, juce::dontSendNotification);
        bufLabel.setText (Strings::settingsui::bufferSize, juce::dontSendNotification);
        for (auto* l : { &typeLabel, &inLabel, &outLabel, &rateLabel, &bufLabel, &latencyLabel, &cpuLabel, &noteLabel, &channelsLabel })
        {
            l->setFont (theme().uiFont (12.5f));
            l->setColour (juce::Label::textColourId, theme().text);
        }
        noteLabel.setColour (juce::Label::textColourId, theme().dimText);
        noteLabel.setFont (theme().uiFont (11.5f));
        channelsLabel.setFont (theme().uiFont (12.5f, 600));
        channelsLabel.setText (Strings::settingsui::channels, juce::dontSendNotification);
        latencyLabel.setFont (theme().numFont (12.5f));
        cpuLabel.setFont (theme().numFont (11.5f));
        cpuLabel.setColour (juce::Label::textColourId, theme().dimText);
#if JUCE_MAC
        noteLabel.setText (Strings::settingsui::driverNoteMac, juce::dontSendNotification);
#else
        noteLabel.setText (Strings::settingsui::driverNoteDev, juce::dontSendNotification);
#endif
        for (auto* c : std::initializer_list<juce::Component*> { &typeBox, &inBox, &outBox, &rateBox, &bufBox, &latencyLabel, &cpuLabel, &noteLabel,
                                                                  &channelsLabel, &testButton, &channelList })
            addAndMakeVisible (c);
        for (auto* b : { &typeBox, &inBox, &outBox, &rateBox, &bufBox })
            styleCombo (*b);
        testButton.setButtonText (Strings::settingsui::testTone);
        testButton.onClick = [this] {
            if (engine.getDeviceManager().getCurrentAudioDevice() == nullptr)
                showMessage (Strings::settingsui::title, Strings::settingsui::noDevice);
            else
                engine.playTestTone();
        };
#if JUCE_MAC
        addAndMakeVisible (macButton);
        macButton.setButtonText (Strings::settingsui::audioMidiSetup);
        macButton.onClick = [] { juce::File ("/System/Applications/Utilities/Audio MIDI Setup.app").startAsProcess(); };
#endif
        typeBox.onChange = [this] {
            if (! populating)
                engine.getDeviceManager().setCurrentAudioDeviceType (typeBox.getText(), true);
        };
        inBox.onChange = [this] { apply ([this] (juce::AudioDeviceManager::AudioDeviceSetup& setup) { setup.inputDeviceName = inBox.getSelectedId() == 1 ? juce::String() : inBox.getText(); }); };
        outBox.onChange = [this] { apply ([this] (juce::AudioDeviceManager::AudioDeviceSetup& setup) { setup.outputDeviceName = outBox.getText(); }); };
        rateBox.onChange = [this] { apply ([this] (juce::AudioDeviceManager::AudioDeviceSetup& setup) { setup.sampleRate = rateBox.getSelectedId(); }); };
        bufBox.onChange = [this] { apply ([this] (juce::AudioDeviceManager::AudioDeviceSetup& setup) { setup.bufferSize = bufBox.getSelectedId(); }); };
        engine.getDeviceManager().addChangeListener (this);
        refresh();
        startTimer (500);
    }

    ~SoundPage() override { engine.getDeviceManager().removeChangeListener (this); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        auto row = [&] (juce::Label& label, juce::Component& box) {
            auto line = r.removeFromTop (kRowHeight);
            label.setBounds (line.removeFromLeft (kLabelWidth));
            box.setBounds (line.removeFromLeft (juce::jmin (line.getWidth(), 360)));
            r.removeFromTop (6);
        };
        row (typeLabel, typeBox);
        noteLabel.setBounds (r.removeFromTop (20));
        r.removeFromTop (6);
        row (inLabel, inBox);
        row (outLabel, outBox);
        row (rateLabel, rateBox);
        row (bufLabel, bufBox);
        auto line = r.removeFromTop (kRowHeight);
        latencyLabel.setBounds (line.removeFromLeft (440));
        r.removeFromTop (4);
        auto buttons = r.removeFromTop (30);
        testButton.setBounds (buttons.removeFromLeft (160));
#if JUCE_MAC
        buttons.removeFromLeft (8);
        macButton.setBounds (buttons.removeFromLeft (240));
#endif
        cpuLabel.setBounds (buttons.removeFromRight (240));
        r.removeFromTop (12);
        channelsLabel.setBounds (r.removeFromTop (24));
        channelList.setBounds (r);
    }

    void refresh() override
    {
        populating = true;
        auto& dm = engine.getDeviceManager();
        const auto& types = dm.getAvailableDeviceTypes();
        typeBox.clear (juce::dontSendNotification);
        for (int i = 0; i < types.size(); ++i)
            typeBox.addItem (types[i]->getTypeName(), i + 1);
        for (int i = 0; i < types.size(); ++i)
            if (types[i]->getTypeName() == dm.getCurrentAudioDeviceType())
                typeBox.setSelectedId (i + 1, juce::dontSendNotification);

        const auto setup = dm.getAudioDeviceSetup();
        auto* type = dm.getCurrentDeviceTypeObject();
        inBox.clear (juce::dontSendNotification);
        outBox.clear (juce::dontSendNotification);
        inBox.addItem (Strings::settingsui::none, 1);
        inBox.setSelectedId (1, juce::dontSendNotification);
        if (type != nullptr)
        {
            const auto ins = type->getDeviceNames (true), outs = type->getDeviceNames (false);
            for (int i = 0; i < ins.size(); ++i)
            {
                inBox.addItem (ins[i], i + 2);
                if (ins[i] == setup.inputDeviceName)
                    inBox.setSelectedId (i + 2, juce::dontSendNotification);
            }
            for (int i = 0; i < outs.size(); ++i)
            {
                outBox.addItem (outs[i], i + 1);
                if (outs[i] == setup.outputDeviceName)
                    outBox.setSelectedId (i + 1, juce::dontSendNotification);
            }
        }

        rateBox.clear (juce::dontSendNotification);
        bufBox.clear (juce::dontSendNotification);
        auto* device = dm.getCurrentAudioDevice();
        if (device != nullptr)
        {
            for (const auto rate : device->getAvailableSampleRates())
                rateBox.addItem (Strings::settingsui::hz (rate), juce::roundToInt (rate));
            rateBox.setSelectedId (juce::roundToInt (device->getCurrentSampleRate()), juce::dontSendNotification);
            for (const auto size : device->getAvailableBufferSizes())
                bufBox.addItem (Strings::settingsui::samples (size), size);
            bufBox.setSelectedId (device->getCurrentBufferSizeSamples(), juce::dontSendNotification);
        }
        for (auto* b : { &inBox, &outBox, &rateBox, &bufBox })
            b->setEnabled (device != nullptr || b == &inBox || b == &outBox);
        populating = false;
        rebuildChannels();
        updateReadouts();
    }

    // The latency text and the CPU load; the owner of the timer is this page.
    juce::String latencyText() const { return latencyLabel.getText(); }

private:
    class ChannelRow final : public juce::Component
    {
    public:
        ChannelRow (SoundPage& o, const juce::String& device, bool in, int ch, const juce::String& hardwareName, bool on)
            : owner (o), deviceName (device), isInput (in), channel (ch)
        {
            toggle.setButtonText (hardwareName);
            toggle.setToggleState (on, juce::dontSendNotification);
            toggle.onClick = [this] { owner.setChannelActive (isInput, channel, toggle.getToggleState()); };
            name.setText (owner.settings.channelName (device, in, ch), juce::dontSendNotification);
            name.setTextToShowWhenEmpty (Strings::settingsui::channelNameHint, theme().dimText);
            name.onTextChange = [this] { owner.settings.setChannelName (deviceName, isInput, channel, name.getText().trim()); };
            addAndMakeVisible (toggle);
            addAndMakeVisible (name);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 2);
            toggle.setBounds (r.removeFromLeft (r.getWidth() / 2));
            name.setBounds (r.reduced (4, 0));
        }

    private:
        SoundPage& owner;
        juce::String deviceName;
        bool isInput;
        int channel;
        juce::ToggleButton toggle;
        juce::TextEditor name;
    };

    class Heading final : public juce::Component
    {
    public:
        explicit Heading (const juce::String& t) : text (t) {}
        void paint (juce::Graphics& g) override
        {
            g.setColour (theme().dimText);
            g.setFont (theme().uiFont (11.0f, 600));
            g.drawText (text.toUpperCase(), getLocalBounds().withTrimmedLeft (8), juce::Justification::centredLeft, true);
        }

    private:
        juce::String text;
    };

    void apply (const std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>& change)
    {
        if (populating)
            return;
        auto& dm = engine.getDeviceManager();
        auto setup = dm.getAudioDeviceSetup();
        change (setup);
        const auto error = dm.setAudioDeviceSetup (setup, true);
        if (error.isNotEmpty())
            showMessage (Strings::settingsui::title, error);
        refresh();
    }

    void setChannelActive (bool isInput, int channel, bool on)
    {
        apply ([=] (juce::AudioDeviceManager::AudioDeviceSetup& setup) {
            auto& mask = isInput ? setup.inputChannels : setup.outputChannels;
            (isInput ? setup.useDefaultInputChannels : setup.useDefaultOutputChannels) = false;
            mask.setBit (channel, on);
        });
    }

    void rebuildChannels()
    {
        auto& rows = channelList.rows();
        rows.clear();
        auto* device = engine.getDeviceManager().getCurrentAudioDevice();
        if (device == nullptr)
        {
            channelList.update();
            return;
        }
        const auto deviceName = device->getName();
        auto add = [&] (bool in) {
            const auto names = in ? device->getInputChannelNames() : device->getOutputChannelNames();
            const auto active = in ? device->getActiveInputChannels() : device->getActiveOutputChannels();
            rows.add (std::make_unique<Heading> (in ? Strings::settingsui::inputChannels : Strings::settingsui::outputChannels));
            for (int i = 0; i < juce::jmin (names.size(), 32); ++i)
                rows.add (std::make_unique<ChannelRow> (*this, deviceName, in, i, names[i], active[i]));
        };
        add (true);
        add (false);
        channelList.update();
    }

    void updateReadouts()
    {
        auto& dm = engine.getDeviceManager();
        auto* device = dm.getCurrentAudioDevice();
        if (device == nullptr)
            latencyLabel.setText (Strings::settingsui::noDevice, juce::dontSendNotification);
        else
        {
            const double sr = juce::jmax (1.0, device->getCurrentSampleRate());
            const int buffer = device->getCurrentBufferSizeSamples();
            const double in = (device->getInputLatencyInSamples() + buffer) * 1000.0 / sr;
            const double out = (device->getOutputLatencyInSamples() + buffer) * 1000.0 / sr;
            latencyLabel.setText (Strings::settingsui::latency (in, out), juce::dontSendNotification);
        }
        cpuLabel.setText (Strings::settingsui::cpu (juce::roundToInt (dm.getCpuUsage() * 100.0)), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        if (isShowing())
            updateReadouts();
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    AudioEngine& engine;
    Settings& settings;
    bool populating = false;
    juce::Label typeLabel, inLabel, outLabel, rateLabel, bufLabel, latencyLabel, cpuLabel, noteLabel, channelsLabel;
    juce::ComboBox typeBox, inBox, outBox, rateBox, bufBox;
    juce::TextButton testButton;
#if JUCE_MAC
    juce::TextButton macButton;
#endif
    RowList channelList;
};

//==============================================================================
// MIDI
class MidiPage final : public Page
{
public:
    MidiPage (AudioEngine& e, Settings& s) : engine (e), settings (s)
    {
        addAndMakeVisible (inputsLabel);
        addAndMakeVisible (outputsLabel);
        for (auto* l : { &inputsLabel, &outputsLabel, &emptyInputs, &emptyOutputs })
        {
            l->setFont (theme().uiFont (12.5f, l == &inputsLabel || l == &outputsLabel ? 600 : 400));
            l->setColour (juce::Label::textColourId, l == &inputsLabel || l == &outputsLabel ? theme().text : theme().dimText);
        }
        inputsLabel.setText (Strings::settingsui::midiInputs, juce::dontSendNotification);
        outputsLabel.setText (Strings::settingsui::midiOutputs, juce::dontSendNotification);
        emptyInputs.setText (Strings::settingsui::noMidiInputs, juce::dontSendNotification);
        emptyOutputs.setText (Strings::settingsui::noMidiOutputs, juce::dontSendNotification);
        addChildComponent (emptyInputs);
        addChildComponent (emptyOutputs);
        addAndMakeVisible (inputs);
        addAndMakeVisible (outputs);
        connection = juce::MidiDeviceListConnection::make ([this] { refresh(); });
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        inputsLabel.setBounds (r.removeFromTop (24));
        const int inputsHeight = juce::jmin (r.getHeight() / 2, juce::jmax (kRowHeight * 2, kRowHeight * numInputs + 2));
        inputs.setBounds (r.removeFromTop (inputsHeight));
        emptyInputs.setBounds (inputs.getBounds());
        r.removeFromTop (14);
        outputsLabel.setBounds (r.removeFromTop (24));
        const int outputsHeight = juce::jmin (r.getHeight(), juce::jmax (kRowHeight * 2, kRowHeight * numOutputs + 2));
        outputs.setBounds (r.removeFromTop (outputsHeight));
        emptyOutputs.setBounds (outputs.getBounds());
    }

    void refresh() override
    {
        inputs.rows().clear();
        outputs.rows().clear();
        const auto in = juce::MidiInput::getAvailableDevices();
        const auto out = juce::MidiOutput::getAvailableDevices();
        numInputs = in.size();
        numOutputs = out.size();
        for (const auto& d : in)
            inputs.rows().add (std::make_unique<InputRow> (*this, d));
        for (const auto& d : out)
            outputs.rows().add (std::make_unique<NameRow> (d.name));
        inputs.update();
        outputs.update();
        emptyInputs.setVisible (in.isEmpty());
        emptyOutputs.setVisible (out.isEmpty());
        resized();
    }

private:
    class NameRow final : public juce::Component
    {
    public:
        explicit NameRow (const juce::String& n) : text (n) {}
        void paint (juce::Graphics& g) override
        {
            g.setColour (theme().text);
            g.setFont (theme().uiFont (12.5f));
            g.drawText (text, getLocalBounds().withTrimmedLeft (10), juce::Justification::centredLeft, true);
        }

    private:
        juce::String text;
    };

    class InputRow final : public juce::Component
    {
    public:
        InputRow (MidiPage& o, const juce::MidiDeviceInfo& info) : owner (o), identifier (info.identifier), name (info.name)
        {
            play.setButtonText (Strings::settingsui::playTracks);
            play.setToggleState (owner.settings.midiInputPlays (identifier), juce::dontSendNotification);
            play.onClick = [this] {
                owner.settings.setMidiInputPlays (identifier, play.getToggleState());
                owner.settings.save();
                owner.engine.refreshMidiInputs();
            };
            control.setButtonText (Strings::settingsui::control);
            control.setEnabled (false); // controllers arrive with stage 6
            addAndMakeVisible (play);
            addAndMakeVisible (control);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (theme().text);
            g.setFont (theme().uiFont (12.5f));
            g.drawText (name, getLocalBounds().withTrimmedLeft (10).withTrimmedRight (330), juce::Justification::centredLeft, true);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 2);
            control.setBounds (r.removeFromRight (120));
            play.setBounds (r.removeFromRight (190));
        }

    private:
        MidiPage& owner;
        juce::String identifier, name;
        juce::ToggleButton play, control;
    };

    AudioEngine& engine;
    Settings& settings;
    juce::Label inputsLabel, outputsLabel, emptyInputs, emptyOutputs;
    RowList inputs, outputs;
    int numInputs = 0, numOutputs = 0;
    juce::MidiDeviceListConnection connection;
};

//==============================================================================
// Recording: the manual correction of the MIDI delay (docs/spec/02, task 2.2)
class RecordingPage final : public Page, private juce::Timer
{
public:
    RecordingPage (AudioEngine& e, Settings&) : engine (e)
    {
        for (auto* l : { &heading, &autoLabel, &autoValue, &note })
            addAndMakeVisible (*l);
        heading.setText (Strings::recording::correction, juce::dontSendNotification);
        autoLabel.setText (Strings::recording::automaticCompensation, juce::dontSendNotification);
        note.setText (Strings::recording::correctionNote, juce::dontSendNotification);
        note.setJustificationType (juce::Justification::topLeft);

        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 24);
        slider.setRange (-50.0, 50.0, 0.1);
        slider.setDoubleClickReturnValue (true, 0.0);
        slider.setValue (engine.getRecordCorrectionMs(), juce::dontSendNotification);
        slider.textFromValueFunction = [] (double v) { return Strings::recording::signedMs (v); };
        slider.valueFromTextFunction = [] (const juce::String& text) {
            return juce::jlimit (-50.0, 50.0, text.replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-").replace (",", ".").getDoubleValue());
        };
        slider.onValueChange = [this] { engine.setRecordCorrectionMs (slider.getValue()); };
        addAndMakeVisible (slider);

        reset.setButtonText (Strings::recording::correctionReset);
        reset.onClick = [this] { slider.setValue (0.0, juce::sendNotificationSync); };
        addAndMakeVisible (reset);

        // Sound (docs/spec/05-audio-recording.md, task 5.4): the latency recordings are moved by, the loopback test, the format
        for (auto* l : { &audioHeading, &driverLabel, &driverValue, &testResult, &bitsLabel, &audioNote })
            addAndMakeVisible (*l);
        audioHeading.setText (Strings::audioRec::heading, juce::dontSendNotification);
        driverLabel.setText (Strings::audioRec::driverLatency, juce::dontSendNotification);
        bitsLabel.setText (Strings::audioRec::bitDepth, juce::dontSendNotification);
        audioNote.setText (Strings::audioRec::testNote, juce::dontSendNotification);
        audioNote.setJustificationType (juce::Justification::topLeft);
        audioSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        audioSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 24);
        audioSlider.setRange (-100.0, 100.0, 0.1);
        audioSlider.setDoubleClickReturnValue (true, 0.0);
        audioSlider.setValue (engine.getAudioLatencyCorrectionMs(), juce::dontSendNotification);
        audioSlider.textFromValueFunction = slider.textFromValueFunction;
        audioSlider.valueFromTextFunction = [] (const juce::String& text) {
            return juce::jlimit (-100.0, 100.0, text.replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-").replace (",", ".").getDoubleValue());
        };
        audioSlider.onValueChange = [this] { engine.setAudioLatencyCorrectionMs (audioSlider.getValue()); };
        addAndMakeVisible (audioSlider);
        measure.setButtonText (Strings::audioRec::measure);
        measure.onClick = [this] { startTest(); };
        addAndMakeVisible (measure);
        addAndMakeVisible (testInput);
        bits.addItem ("16", 16);
        bits.addItem ("24", 24);
        bits.addItem (Strings::audioRec::float32, 32);
        bits.setSelectedId (engine.getSettings().recordBitDepth(), juce::dontSendNotification);
        bits.onChange = [this] { engine.getSettings().setRecordBitDepth (bits.getSelectedId()); };
        addAndMakeVisible (bits);
        restyle();
        refresh();
        startTimer (500); // the device can change while the window is open
    }

    void refresh() override
    {
        restyle();
        const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 44100.0;
        const int manual = juce::roundToInt (engine.getRecordCorrectionMs() * sr / 1000.0);
        autoValue.setText (Strings::recording::autoMs ((double) (engine.getRecordCompensationSamples() - manual) * 1000.0 / sr), juce::dontSendNotification);
        if (std::abs (slider.getValue() - engine.getRecordCorrectionMs()) > 0.05 && ! slider.isMouseButtonDown())
            slider.setValue (engine.getRecordCorrectionMs(), juce::dontSendNotification);

        driverValue.setText (Strings::recording::autoMs (engine.getDriverRoundTrip() * 1000.0 / sr), juce::dontSendNotification);
        if (std::abs (audioSlider.getValue() - engine.getAudioLatencyCorrectionMs()) > 0.05 && ! audioSlider.isMouseButtonDown())
            audioSlider.setValue (engine.getAudioLatencyCorrectionMs(), juce::dontSendNotification);
        const auto names = engine.getInputChannelNames();
        if (names.size() != testInput.getNumItems())
        {
            testInput.clear (juce::dontSendNotification);
            for (int i = 0; i < names.size(); ++i)
                testInput.addItem (Strings::audioInput::mono (i), i + 1);
            testInput.setSelectedId (1, juce::dontSendNotification);
        }
        measure.setEnabled (names.size() > 0 && ! engine.isLatencyTestRunning());
        if (testing && engine.isLatencyTestDone())
        {
            testing = false;
            const auto r = engine.takeLatencyTestResult();
            if (r.ok)
            {
                engine.setAudioLatencyCorrectionMs (r.correctionMs);
                testResult.setText (Strings::audioRec::measured (r.roundTripSamples * 1000.0 / sr, r.correctionMs), juce::dontSendNotification);
            }
            else
                testResult.setText (Strings::audioRec::notFound, juce::dontSendNotification);
        }
    }

    void startTest()
    {
        if (engine.isPlaying())
            engine.stop();
        if (engine.startLatencyTest (juce::jmax (0, testInput.getSelectedId() - 1)))
        {
            testing = true;
            testResult.setText (Strings::audioRec::running, juce::dontSendNotification);
            startTimer (100);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        heading.setBounds (r.removeFromTop (24));
        auto line = r.removeFromTop (kRowHeight + 6);
        reset.setBounds (line.removeFromRight (100).reduced (0, 2));
        line.removeFromRight (10);
        slider.setBounds (line);
        r.removeFromTop (14);
        auto row = r.removeFromTop (kRowHeight);
        autoLabel.setBounds (row.removeFromLeft (230));
        autoValue.setBounds (row.removeFromLeft (160));
        r.removeFromTop (14);
        note.setBounds (r.removeFromTop (54));

        audioHeading.setBounds (r.removeFromTop (24));
        auto aline = r.removeFromTop (kRowHeight + 6);
        audioSlider.setBounds (aline);
        r.removeFromTop (8);
        auto drow = r.removeFromTop (kRowHeight);
        driverLabel.setBounds (drow.removeFromLeft (230));
        driverValue.setBounds (drow.removeFromLeft (160));
        r.removeFromTop (8);
        auto trow = r.removeFromTop (kRowHeight + 4);
        measure.setBounds (trow.removeFromLeft (190).reduced (0, 2));
        trow.removeFromLeft (10);
        testInput.setBounds (trow.removeFromLeft (120).reduced (0, 2));
        trow.removeFromLeft (10);
        testResult.setBounds (trow);
        r.removeFromTop (8);
        auto brow = r.removeFromTop (kRowHeight + 4);
        bitsLabel.setBounds (brow.removeFromLeft (230));
        bits.setBounds (brow.removeFromLeft (120).reduced (0, 2));
        r.removeFromTop (8);
        audioNote.setBounds (r.removeFromTop (54));
    }

private:
    void timerCallback() override { refresh(); }
    void restyle()
    {
        heading.setFont (theme().uiFont (12.5f, 600));
        heading.setColour (juce::Label::textColourId, theme().text);
        autoLabel.setFont (theme().uiFont (12.5f));
        autoLabel.setColour (juce::Label::textColourId, theme().text);
        autoValue.setFont (theme().numFont (12.5f));
        autoValue.setColour (juce::Label::textColourId, theme().text);
        note.setFont (theme().uiFont (11.5f));
        note.setColour (juce::Label::textColourId, theme().dimText);
        audioHeading.setFont (theme().uiFont (12.5f, 600));
        audioHeading.setColour (juce::Label::textColourId, theme().text);
        for (auto* l : { &driverLabel, &bitsLabel, &testResult })
        {
            l->setFont (theme().uiFont (12.5f));
            l->setColour (juce::Label::textColourId, theme().text);
        }
        driverValue.setFont (theme().numFont (12.5f));
        driverValue.setColour (juce::Label::textColourId, theme().text);
        audioNote.setFont (theme().uiFont (11.5f));
        audioNote.setColour (juce::Label::textColourId, theme().dimText);
        for (auto* sl : { &audioSlider })
        {
            sl->setColour (juce::Slider::trackColourId, theme().accent);
            sl->setColour (juce::Slider::thumbColourId, theme().accent);
            sl->setColour (juce::Slider::backgroundColourId, theme().panel2);
            sl->setColour (juce::Slider::textBoxTextColourId, theme().text);
            sl->setColour (juce::Slider::textBoxOutlineColourId, theme().line);
            sl->setColour (juce::Slider::textBoxBackgroundColourId, theme().panel);
        }
        slider.setColour (juce::Slider::trackColourId, theme().accent);
        slider.setColour (juce::Slider::thumbColourId, theme().accent);
        slider.setColour (juce::Slider::backgroundColourId, theme().panel2);
        slider.setColour (juce::Slider::textBoxTextColourId, theme().text);
        slider.setColour (juce::Slider::textBoxOutlineColourId, theme().line);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, theme().panel);
    }

    AudioEngine& engine;
    juce::Label heading, autoLabel, autoValue, note;
    juce::Slider slider;
    juce::Label audioHeading, driverLabel, driverValue, testResult, bitsLabel, audioNote;
    juce::Slider audioSlider;
    juce::TextButton measure;
    juce::ComboBox testInput, bits;
    bool testing = false;
    juce::TextButton reset;
};

//==============================================================================
// Appearance
class AppearancePage final : public Page
{
public:
    explicit AppearancePage (Settings& s) : settings (s)
    {
        for (int pct = 80; pct <= 150; pct += 10)
            scale.addItem (juce::String (pct) + " %", pct);
        scale.setSelectedId (juce::roundToInt (settings.uiScale () * 100), juce::dontSendNotification);
        scale.onChange = [this]
        {
            settings.setUiScale (scale.getSelectedId () / 100.0);
            juce::Desktop::getInstance ().setGlobalScaleFactor ((float)settings.uiScale ());
        };
        custom.setButtonText (Strings::appearance::custom);
        gradient.setButtonText (Strings::appearance::gradient);
        opaque.setButtonText (Strings::appearance::opaque);
        animations.setButtonText (Strings::settingsui::animations);
        animations.onClick = [this] { settings.setAnimations (animations.getToggleState()); };
        reset.setButtonText (Strings::appearance::reset);
        reset.onClick = [this]
        {
            commit (Appearance ());
            refresh ();
        };
        for (auto* c : std::initializer_list<juce::Component*>{
               &scale, &primary, &secondary, &custom, &gradient, &opaque, &animations, &angle, &glow, &depth, &reset })
            addAndMakeVisible (c);
        for (auto* e : { &primary, &secondary })
        {
            e->setInputRestrictions (7, "#0123456789abcdefABCDEF");
            e->setHelpText (Strings::appearance::colourHelp);
            e->onReturnKey = [this] { applyColours (); };
            e->onFocusLost = [this] { applyColours (); };
        }
        for (auto* slider : { &angle, &glow, &depth })
        {
            slider->setSliderStyle (juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
            slider->setRange (0, slider == &angle ? 360 : 100, 1);
            slider->setTextValueSuffix (slider == &angle ? juce::String::fromUTF8 ("°") : " %");
            slider->onValueChange = [this] { applyControls (); };
        }
        for (auto* b : { &custom, &gradient, &opaque })
            b->onClick = [this] { applyControls (); };
        for (int i = 0; i < 8; ++i)
        {
            auto& b = swatches[(size_t)i];
            b.setButtonText (juce::String (i + 1));
            b.setHelpText (Strings::appearance::primary);
            b.onClick = [this, i]
            {
                auto a = ThemeManager::get ().appearance ();
                a.customAccent = true;
                a.primary = theme ().palette (i * 2);
                commit (a);
            };
            addAndMakeVisible (b);
        }
        languageBox.addItem ("English", 1);
        languageBox.addItem (juce::String::fromUTF8 ("Русский"), 2);
        languageBox.setSelectedId (settings.language() == "ru" ? 2 : 1, juce::dontSendNotification);
        languageBox.onChange = [this] {
            settings.setLanguage (languageBox.getSelectedId() == 2 ? "ru" : "en");
            languageNote.setVisible (Strings::languageFromCode (settings.language()) != Strings::language());
        };
        addAndMakeVisible (languageBox);
        languageNote.setText (Strings::settingsui::languageRestart, juce::dontSendNotification);
        languageNote.setFont (theme().uiFont (11.5f));
        languageNote.setVisible (Strings::languageFromCode (settings.language()) != Strings::language());
        addChildComponent (languageNote);
        addAndMakeVisible (themeList);
        themeList.onChange = [this]
        {
            const auto& themes = ThemeManager::get ().all ();
            const int index = themeList.getSelectedId () - 1;
            if (index >= 0 && index < (int)themes.size ())
            {
                settings.setTheme (themes[(size_t)index].id);
                ThemeManager::get ().setCurrent (themes[(size_t)index].id);
            }
        };
        refresh ();
    }

    void refresh () override
    {
        const auto& a = ThemeManager::get ().appearance ();
        for (auto* editor : { &primary, &secondary })
        {
            editor->applyColourToAllText (theme ().text);
            editor->setColour (juce::TextEditor::backgroundColourId, theme ().panel);
        }
        custom.setToggleState (a.customAccent, juce::dontSendNotification);
        gradient.setToggleState (a.gradient, juce::dontSendNotification);
        opaque.setToggleState (a.reduceTransparency, juce::dontSendNotification);
        animations.setToggleState (settings.animations (), juce::dontSendNotification);
        if (!primary.hasKeyboardFocus (true))
            primary.setText ("#" + a.primary.toDisplayString (false), false);
        if (!secondary.hasKeyboardFocus (true))
            secondary.setText ("#" + a.secondary.toDisplayString (false), false);
        angle.setValue (a.angle, juce::dontSendNotification);
        glow.setValue (a.glow * 100, juce::dontSendNotification);
        depth.setValue (a.glass * 100, juce::dontSendNotification);
        for (int i = 0; i < 8; ++i)
        {
            const auto c = theme ().palette (i * 2);
            swatches[(size_t)i].setColour (juce::TextButton::buttonColourId, c);
            swatches[(size_t)i].setColour (juce::TextButton::textColourOffId, theme ().inkOn (c));
        }
        languageNote.setColour (juce::Label::textColourId, theme ().dimText);
        const bool optical = theme ().glass && !a.reduceTransparency;
        angle.setEnabled (theme ().glass && a.gradient);
        glow.setEnabled (optical);
        depth.setEnabled (optical);
        gradient.setEnabled (theme ().glass);
        opaque.setEnabled (theme ().glass);
        themeList.clear (juce::dontSendNotification);
        int themeIndex = 0;
        for (const auto& t : ThemeManager::get ().all ())
        {
            themeList.addItem (t.name, ++themeIndex);
            if (t.id == theme ().id)
                themeList.setSelectedId (themeIndex, juce::dontSendNotification);
        }
        themeList.setVisible (themeIndex > 6);
        cards.clear();
        for (const auto& t : ThemeManager::get().all())
        {
            if (cards.size () >= 6)
                break;
            auto b = std::make_unique<juce::TextButton> (t.name);
            b->setClickingTogglesState (true);
            b->setToggleState (t.id == theme ().id, juce::dontSendNotification);
            b->setColour (juce::TextButton::buttonColourId, t.panel);
            b->setColour (juce::TextButton::textColourOffId, t.text);
            b->onClick = [this, id = t.id]
            {
                settings.setTheme (id);
                ThemeManager::get ().setCurrent (id);
            };
            addAndMakeVisible (*b);
            cards.push_back (std::move (b));
        }
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme ();
        g.setColour (t.text);
        g.setFont (t.uiFont (13.0f, 600));
        g.drawText (Strings::settingsui::themeHeading, 20, 14, getWidth () - 40, 24, juce::Justification::centredLeft);
        g.setFont (t.uiFont (11.5f));
        g.setColour (t.dimText);
        g.drawText (Strings::appearance::primary, 20, 183, 180, 20, juce::Justification::centredLeft);
        g.drawText (Strings::appearance::secondary, getWidth () / 2, 183, 180, 20, juce::Justification::centredLeft);
        const juce::String labels[]{ Strings::appearance::angle,
                                     Strings::appearance::glow,
                                     Strings::appearance::glass };
        for (int i = 0; i < 3; ++i)
            g.drawText (labels[i], 20, 286 + i * 34, 160, 30, juce::Justification::centredLeft);
        g.drawText (Strings::settingsui::scale, 20, 438, 170, 28, juce::Justification::centredLeft);
        g.drawText (Strings::settingsui::language, 20, 519, 150, 28, juce::Justification::centredLeft);
        g.setFont (t.uiFont (10.0f, 600));
        g.drawText (
          Strings::appearance::preview, 20, getHeight () - 27, getWidth () - 40, 20, juce::Justification::centredLeft);
    }

    void resized () override
    {
        const int columns = 3, gap = 8, width = (getWidth () - 40 - gap * 2) / columns;
        for (size_t i = 0; i < cards.size (); ++i)
            cards[i]->setBounds (20 + (int)(i % 3) * (width + gap), 46 + (int)(i / 3) * 39, width, 32);
        custom.setBounds (20, 132, 290, 30);
        themeList.setBounds (320, 132, getWidth () - 340, 30);
        const int half = getWidth () / 2;
        primary.setBounds (20, 206, half - 40, 29);
        secondary.setBounds (half, 206, half - 20, 29);
        for (int i = 0; i < 8; ++i)
            swatches[(size_t)i].setBounds (20 + i * 38, 245, 30, 26);
        angle.setBounds (180, 286, getWidth () - 205, 30);
        glow.setBounds (180, 320, getWidth () - 205, 30);
        depth.setBounds (180, 354, getWidth () - 205, 30);
        gradient.setBounds (20, 399, 160, 28);
        opaque.setBounds (180, 399, getWidth () - 200, 28);
        scale.setBounds (180, 438, 100, 28);
        animations.setBounds (300, 438, getWidth () - 320, 28);
        reset.setBounds (20, 483, 220, 28);
        languageBox.setBounds (180, 519, 110, 28);
        languageNote.setBounds (300, 519, getWidth () - 320, 28);
    }

  private:
    void commit (const Appearance& a)
    {
        ThemeManager::get ().setAppearance (a);
        settings.setAppearance (ThemeManager::get ().appearance ().toJson ());
    }
    void applyColours ()
    {
        const auto valid = [] (juce::String s)
        {
            s = s.trimCharactersAtStart ("#");
            return s.length () == 6 && s.containsOnly ("0123456789abcdefABCDEF");
        };
        primary.setColour (juce::TextEditor::outlineColourId,
                           valid (primary.getText ()) ? theme ().line : theme ().record);
        secondary.setColour (juce::TextEditor::outlineColourId,
                             valid (secondary.getText ()) ? theme ().line : theme ().record);
        if (!valid (primary.getText ()) || !valid (secondary.getText ()))
            return;
        auto a = ThemeManager::get ().appearance ();
        const auto old = a;
        a.primary = juce::Colour::fromString ("ff" + primary.getText ().trimCharactersAtStart ("#"));
        a.secondary = juce::Colour::fromString ("ff" + secondary.getText ().trimCharactersAtStart ("#"));
        if (a.primary == old.primary && a.secondary == old.secondary)
            return;
        a.customAccent = true;
        commit (a);
    }
    void applyControls ()
    {
        auto a = ThemeManager::get ().appearance ();
        a.customAccent = custom.getToggleState ();
        a.gradient = gradient.getToggleState ();
        a.reduceTransparency = opaque.getToggleState ();
        a.angle = (float)angle.getValue ();
        a.glow = (float)glow.getValue () / 100.0f;
        a.glass = (float)depth.getValue () / 100.0f;
        commit (a);
    }
    Settings& settings;
    juce::ComboBox scale, themeList, languageBox;
    juce::Label languageNote;
    juce::TextEditor primary, secondary;
    juce::ToggleButton custom, gradient, opaque, animations;
    juce::Slider angle, glow, depth;
    juce::TextButton reset;
    std::array<juce::TextButton, 8> swatches;
    std::vector<std::unique_ptr<juce::TextButton>> cards;
};

//==============================================================================
// Library
class LibraryPage final : public Page
{
public:
    LibraryPage (Settings& s, SettingsWindow::Hooks& h) : settings (s), hooks (h)
    {
        for (auto* l : { &pathHeading, &placesHeading, &placesEmpty })
        {
            l->setFont (theme().uiFont (12.5f, l == &placesEmpty ? 400 : 600));
            l->setColour (juce::Label::textColourId, l == &placesEmpty ? theme().dimText : theme().text);
        }
        pathValue.setFont (theme().numFont (12.0f));
        pathValue.setColour (juce::Label::textColourId, theme().text);
        pathHeading.setText (Strings::settingsui::libraryPath, juce::dontSendNotification);
        placesHeading.setText (Strings::settingsui::placesHeading, juce::dontSendNotification);
        placesEmpty.setText (Strings::settingsui::noPlaces, juce::dontSendNotification);
        changeButton.setButtonText (Strings::settingsui::change);
        defaultButton.setButtonText (Strings::settingsui::byDefault);
        addButton.setButtonText (Strings::settingsui::add);
        rescanButton.setButtonText (Strings::settingsui::rescan);
        for (auto* c : std::initializer_list<juce::Component*> { &pathHeading, &pathValue, &placesHeading, &placesEmpty, &changeButton, &defaultButton,
                                                                  &addButton, &rescanButton, &places, &copySamples })
            addAndMakeVisible (c);
        copySamples.setButtonText (Strings::settingsui::copySamples);
        copySamples.setTooltip (Strings::settingsui::copySamplesHint);
        copySamples.setToggleState (settings.copySamplesIntoProject(), juce::dontSendNotification);
        copySamples.onClick = [this] { settings.setCopySamplesIntoProject (copySamples.getToggleState()); };
        changeButton.onClick = [this] { chooseLibrary(); };
        defaultButton.onClick = [this] { setLibraryRoot (library::UserLibrary::defaultRoot(), true); };
        addButton.onClick = [this] { addPlace(); };
        rescanButton.onClick = [this] {
            if (hooks.scanPlugins)
                hooks.scanPlugins();
        };
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        pathHeading.setBounds (r.removeFromTop (24));
        pathValue.setBounds (r.removeFromTop (kRowHeight));
        auto buttons = r.removeFromTop (30);
        changeButton.setBounds (buttons.removeFromLeft (130));
        buttons.removeFromLeft (8);
        defaultButton.setBounds (buttons.removeFromLeft (130));
        r.removeFromTop (12);
        copySamples.setBounds (r.removeFromTop (kRowHeight));
        r.removeFromTop (12);
        placesHeading.setBounds (r.removeFromTop (24));
        auto bottom = r.removeFromBottom (30);
        rescanButton.setBounds (bottom.removeFromRight (280));
        addButton.setBounds (bottom.removeFromLeft (130));
        r.removeFromBottom (10);
        places.setBounds (r);
        placesEmpty.setBounds (r.reduced (10, 0).removeFromTop (kRowHeight));
    }

    void refresh() override
    {
        const auto path = settings.libraryPath().isNotEmpty() ? settings.libraryPath() : library::UserLibrary::defaultRoot().getFullPathName();
        pathValue.setText (path, juce::dontSendNotification);
        places.rows().clear();
        const auto folders = settings.placeFolders();
        for (const auto& f : folders)
            places.rows().add (std::make_unique<PlaceRow> (*this, f));
        places.update();
        placesEmpty.setVisible (folders.isEmpty());
    }

private:
    class PlaceRow final : public juce::Component
    {
    public:
        PlaceRow (LibraryPage& o, const juce::String& p) : owner (o), path (p)
        {
            button.setButtonText (Strings::settingsui::remove);
            button.onClick = [this] {
                auto folders = owner.settings.placeFolders();
                folders.removeString (path);
                owner.settings.setPlaceFolders (folders);
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LibraryPage> (&owner)] {
                    if (safe != nullptr)
                    {
                        safe->refresh();
                        if (safe->hooks.placesChanged)
                            safe->hooks.placesChanged();
                    }
                });
            };
            addAndMakeVisible (button);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (theme().text);
            g.setFont (theme().numFont (11.5f));
            g.drawText (path, getLocalBounds().withTrimmedLeft (10).withTrimmedRight (110), juce::Justification::centredLeft, true);
        }
        void resized() override { button.setBounds (getLocalBounds().reduced (4, 2).removeFromRight (96)); }

    private:
        LibraryPage& owner;
        juce::String path;
        juce::TextButton button;
    };

    void setLibraryRoot (const juce::File& root, bool isDefault)
    {
        settings.setLibraryPath (isDefault ? juce::String() : root.getFullPathName());
        refresh();
        if (hooks.libraryRootChanged)
            hooks.libraryRootChanged (root);
    }

    void chooseLibrary()
    {
        chooser = std::make_unique<juce::FileChooser> (Strings::settingsui::chooseLibrary, library::UserLibrary::defaultRoot().getParentDirectory(), "", useNativeChooser());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<LibraryPage> (this)] (const juce::FileChooser& fc) {
                                  if (safe != nullptr && fc.getResult() != juce::File())
                                      safe->setLibraryRoot (fc.getResult(), false);
                              });
    }

    void addPlace()
    {
        chooser = std::make_unique<juce::FileChooser> (Strings::browser::addFolderTitle, juce::File::getSpecialLocation (juce::File::userMusicDirectory), "", useNativeChooser());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<LibraryPage> (this)] (const juce::FileChooser& fc) {
                                  if (safe == nullptr || fc.getResult() == juce::File())
                                      return;
                                  auto folders = safe->settings.placeFolders();
                                  folders.addIfNotAlreadyThere (fc.getResult().getFullPathName());
                                  safe->settings.setPlaceFolders (folders);
                                  safe->refresh();
                                  if (safe->hooks.placesChanged)
                                      safe->hooks.placesChanged();
                              });
    }

    Settings& settings;
    SettingsWindow::Hooks& hooks;
    juce::Label pathHeading, pathValue, placesHeading, placesEmpty;
    juce::TextButton changeButton, defaultButton, addButton, rescanButton;
    juce::ToggleButton copySamples;
    RowList places;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// Plug-ins: the folders a VST3 scan looks in, and the scan itself.
class PluginsPage final : public Page
{
public:
    PluginsPage (AudioEngine& e, Settings& s, SettingsWindow::Hooks& h) : engine (e), settings (s), hooks (h)
    {
        for (auto* l : { &heading, &hint, &auNote, &found })
        {
            l->setFont (theme().uiFont (12.5f, l == &heading ? 600 : 400));
            l->setColour (juce::Label::textColourId, l == &heading ? theme().text : theme().dimText);
            l->setJustificationType (juce::Justification::topLeft);
        }
        heading.setText (Strings::settingsui::vst3Folders, juce::dontSendNotification);
        hint.setText (Strings::settingsui::vst3FoldersHint, juce::dontSendNotification);
        auNote.setText (Strings::settingsui::auNote, juce::dontSendNotification);
        addButton.setButtonText (Strings::settingsui::addFolder);
        defaultButton.setButtonText (Strings::settingsui::byDefault);
        scanButton.setButtonText (Strings::settingsui::scanNow);
        listButton.setButtonText (Strings::settingsui::openPluginList);
        addButton.onClick = [this] { addFolder(); };
        defaultButton.onClick = [this] {
            pluginfolders::setVst3Folders (props(), engine.getFormatManager(), {});
            refresh();
        };
        scanButton.onClick = [this] {
            if (hooks.scanPluginFolders)
                hooks.scanPluginFolders();
        };
        listButton.onClick = [this] {
            if (hooks.scanPlugins)
                hooks.scanPlugins();
        };
        for (auto* c : std::initializer_list<juce::Component*> { &heading, &hint, &folders, &addButton, &defaultButton, &auNote, &found, &scanButton,
                                                                  &listButton })
            addAndMakeVisible (c);
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        heading.setBounds (r.removeFromTop (24));
        hint.setBounds (r.removeFromTop (22));
        auto bottom = r.removeFromBottom (30);
        scanButton.setBounds (bottom.removeFromRight (150));
        bottom.removeFromRight (8);
        listButton.setBounds (bottom.removeFromRight (150));
        found.setBounds (bottom);
        r.removeFromBottom (10);
        auNote.setBounds (r.removeFromBottom (40));
        r.removeFromBottom (6);
        auto buttons = r.removeFromBottom (30);
        addButton.setBounds (buttons.removeFromLeft (150));
        buttons.removeFromLeft (8);
        defaultButton.setBounds (buttons.removeFromLeft (130));
        r.removeFromBottom (10);
        folders.setBounds (r);
    }

    void refresh() override
    {
        folders.rows().clear();
        for (const auto& f : pluginfolders::vst3Folders (props(), engine.getFormatManager()))
            folders.rows().add (std::make_unique<FolderRow> (*this, f));
        folders.update();
        found.setText (Strings::settingsui::pluginsFound (engine.getKnownPlugins().getNumTypes()), juce::dontSendNotification);
    }

private:
    class FolderRow final : public juce::Component
    {
    public:
        FolderRow (PluginsPage& o, const juce::String& p) : owner (o), path (p)
        {
            button.setButtonText (Strings::settingsui::remove);
            button.onClick = [this] {
                auto list = pluginfolders::vst3Folders (owner.props(), owner.engine.getFormatManager());
                list.removeString (path);
                // removing the last folder would bring the defaults back: keep the list empty-able only through "Default"
                if (list.isEmpty())
                    return;
                pluginfolders::setVst3Folders (owner.props(), owner.engine.getFormatManager(), list);
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginsPage> (&owner)] {
                    if (safe != nullptr)
                        safe->refresh();
                });
            };
            addAndMakeVisible (button);
        }
        void paint (juce::Graphics& g) override
        {
            const bool exists = juce::File::isAbsolutePath (path) && juce::File (path).isDirectory();
            g.setColour (exists ? theme().text : theme().dimText);
            g.setFont (theme().numFont (11.5f));
            g.drawText (path, getLocalBounds().withTrimmedLeft (10).withTrimmedRight (110), juce::Justification::centredLeft, true);
        }
        void resized() override { button.setBounds (getLocalBounds().reduced (4, 2).removeFromRight (96)); }

    private:
        PluginsPage& owner;
        juce::String path;
        juce::TextButton button;
    };

    juce::PropertiesFile& props() { return settings.getPropertiesFile(); }

    void addFolder()
    {
        chooser = std::make_unique<juce::FileChooser> (Strings::settingsui::chooseVst3Folder, juce::File ("/Library/Audio/Plug-Ins"), "", useNativeChooser());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<PluginsPage> (this)] (const juce::FileChooser& fc) {
                                  if (safe == nullptr || fc.getResult() == juce::File())
                                      return;
                                  auto list = pluginfolders::vst3Folders (safe->props(), safe->engine.getFormatManager());
                                  list.addIfNotAlreadyThere (fc.getResult().getFullPathName());
                                  pluginfolders::setVst3Folders (safe->props(), safe->engine.getFormatManager(), list);
                                  safe->refresh();
                              });
    }

    AudioEngine& engine;
    Settings& settings;
    SettingsWindow::Hooks& hooks;
    juce::Label heading, hint, auNote, found;
    juce::TextButton addButton, defaultButton, scanButton, listButton;
    RowList folders;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
class SettingsWindow::Content final : public juce::Component, private juce::ChangeListener
{
public:
    Content (AudioEngine& e, Settings& s, Hooks& h)
    {
        pages[sound] = std::make_unique<SoundPage> (e, s);
        pages[midi] = std::make_unique<MidiPage> (e, s);
        pages[recording] = std::make_unique<RecordingPage> (e, s);
        pages[appearance] = std::make_unique<AppearancePage> (s);
        pages[library] = std::make_unique<LibraryPage> (s, h);
        pages[plugins] = std::make_unique<PluginsPage> (e, s, h);
        const juce::String names[kNumTabs] { Strings::settingsui::tabSound, Strings::settingsui::tabMidi, Strings::settingsui::tabRecording,
                                             Strings::settingsui::tabAppearance, Strings::settingsui::tabLibrary, Strings::settingsui::tabPlugins };
        for (int i = 0; i < kNumTabs; ++i)
        {
            addChildComponent (*pages[(size_t) i]);
            tabs[(size_t) i].setButtonText (names[i]);
            tabs[(size_t) i].setClickingTogglesState (true);
            tabs[(size_t) i].setRadioGroupId (1);
            tabs[(size_t) i].onClick = [this, i] { show ((Tab) i); };
            addAndMakeVisible (tabs[(size_t) i]);
        }
        ThemeManager::get().addChangeListener (this);
        show (sound);
        setSize (780, 580);
    }
    ~Content() override { ThemeManager::get().removeChangeListener (this); }

    void show (Tab t)
    {
        current = t;
        for (int i = 0; i < kNumTabs; ++i)
        {
            pages[(size_t) i]->setVisible (i == (int) t);
            tabs[(size_t) i].setToggleState (i == (int) t, juce::dontSendNotification);
        }
        pages[(size_t) t]->refresh();
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme().chrome);
        g.setColour (theme().panel);
        g.fillRect (0, 0, kTabWidth, getHeight());
        g.setColour (theme().line);
        g.fillRect (kTabWidth, 0, 1, getHeight());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (kTabWidth).reduced (10, 14);
        for (auto& t : tabs)
        {
            t.setBounds (left.removeFromTop (32));
            left.removeFromTop (4);
        }
        r.removeFromLeft (1);
        for (auto& p : pages)
            p->setBounds (r);
    }

private:
    static constexpr int kTabWidth = 170;
    static constexpr int kNumTabs = 6;
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // a new theme repaints the pages; the colours of labels set at construction are refreshed by rebuilding
        pages[(size_t) current]->refresh();
        sendLookAndFeelChange();
        repaint();
    }

    std::array<std::unique_ptr<Page>, kNumTabs> pages;
    std::array<juce::TextButton, kNumTabs> tabs;
    Tab current = sound;
};

//==============================================================================
SettingsWindow::SettingsWindow (AudioEngine& e, Settings& s, Hooks h)
    : DocumentWindow (Strings::settingsui::title, theme().chrome, DocumentWindow::closeButton), hooks (std::move (h))
{
    setUsingNativeTitleBar (true);
    content = new Content (e, s, hooks);
    setContentOwned (content, true);
    setResizable (false, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::showTab (Tab t)
{
    content->show (t);
}

void SettingsWindow::closeButtonPressed()
{
    if (hooks.closed)
        hooks.closed();
}
} // namespace ui
