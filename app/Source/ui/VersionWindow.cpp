#include "VersionWindow.h"
#include "GlassMaterial.h"

#include "../Strings.h"
#include "../model/TrackTransfer.h"
#include "Dialogs.h"
#include "Theme.h"

namespace ui
{
class VersionWindow::Content final : public juce::Component, private juce::ListBoxModel
{
public:
    Content (project::ProjectSession& s, std::function<void (const juce::String&)> message) : session (s), report (std::move (message))
    {
        list.setModel (this);
        list.setRowHeight (theme().glass ? 50 : 30);
        restoreButton.getProperties().set ("primary", true);
        list.setColour (juce::ListBox::backgroundColourId, theme().chrome);
        list.setColour (juce::ListBox::outlineColourId, theme().line);
        list.setOutlineThickness (theme().glass ? 0 : 1);
        addAndMakeVisible (list);
        for (auto* b : { &labelButton, &restoreButton, &takeButton, &closeButton })
            addAndMakeVisible (*b);
        labelButton.setButtonText (Strings::versions::labelButton);
        restoreButton.setButtonText (Strings::versions::restoreButton);
        closeButton.setButtonText (Strings::versions::closeButton);
        takeButton.setButtonText (Strings::versions::takeTrackButton);
        takeButton.onClick = [this] { takeTrack(); };
        labelButton.onClick = [this] { editLabel(); };
        restoreButton.onClick = [this] { restore(); };
        closeButton.onClick = [this] {
            if (auto* w = findParentComponentOfClass<VersionWindow>())
                w->closeButtonPressed();
        };
        refresh();
        setSize (680, 420);
    }

    void refresh()
    {
        rows = session.versions();
        std::reverse (rows.begin(), rows.end());
        list.updateContent();
        list.repaint();
        updateButtons();
    }

    int numRows() const { return (int) rows.size(); }
    void selectRow (int row) { list.selectRow (row); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        auto buttons = r.removeFromBottom (30);
        r.removeFromBottom (10);
        list.setBounds (r);
        closeButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        restoreButton.setBounds (buttons.removeFromRight (130));
        buttons.removeFromRight (8);
        takeButton.setBounds (buttons.removeFromRight (160));
        buttons.removeFromRight (8);
        labelButton.setBounds (buttons.removeFromRight (110));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme().panel);
        if (rows.empty())
        {
            g.setColour (theme().dimText);
            g.setFont (theme().uiFont (13.0f));
            g.drawFittedText (Strings::versions::empty, list.getBounds().reduced (24), juce::Justification::centred, 3);
        }
    }

private:
    int getNumRows() override { return (int) rows.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) rows.size())
            return;
        const auto& t = theme();
        const auto& v = rows[(size_t) row];
        if (t.glass)
        {
            // Glass: a card per version. A ring (filled for the newest), the number, the label as a pill, the time;
            // what made the version on the line below.
            const auto card = juce::Rectangle<float> (4.0f, 3.0f, (float) width - 8.0f, (float) height - 6.0f);
            if (selected)
            {
                g.setColour (t.accentFill.withAlpha (t.isLight() ? 0.14f : 0.2f));
                g.fillRoundedRectangle (card, 10.0f);
                g.setColour (t.accent.withAlpha (0.7f));
                g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.2f);
            }
            const auto ring = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ 22.0f, (float) height * 0.38f });
            if (row == 0)
            {
                g.setColour (t.play);
                g.fillEllipse (ring);
            }
            else
            {
                g.setColour (selected ? t.accent : t.dimText);
                g.drawEllipse (ring.reduced (0.75f), 1.5f);
            }
            float x = 40.0f;
            const float top = (float) height * 0.38f - 10.0f;
            g.setColour (t.text);
            g.setFont (t.uiFont (14.0f, 600));
            const auto number = "v" + juce::String (v.number);
            g.drawText (number, juce::Rectangle<float> (x, top, 50.0f, 20.0f), juce::Justification::centredLeft, false);
            x += juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), number) + 10.0f;
            if (v.label.isNotEmpty())
            {
                const auto font = t.uiFont (11.5f, 600);
                const float w = juce::jmin (220.0f, juce::GlyphArrangement::getStringWidth (font, v.label) + 16.0f);
                const auto pill = juce::Rectangle<float> (x, top + 1.0f, w, 18.0f);
                glass::accent (g, pill, t.accentFill, 9.0f);
                g.setColour (t.inkOn (t.accentFill));
                g.setFont (font);
                g.drawText (v.label, pill.reduced (7.0f, 0.0f), juce::Justification::centred, true);
                x += w + 10.0f;
            }
            g.setColour (t.dimText);
            g.setFont (t.uiFont (12.0f));
            g.drawText (juce::Time (v.timeMs).formatted ("%d.%m.%Y  %H:%M"), juce::Rectangle<float> (x, top, (float) width - x - 12.0f, 20.0f),
                        juce::Justification::centredLeft, true);
            g.setFont (t.uiFont (11.5f));
            g.drawText (kindText (v.kind), juce::Rectangle<float> (40.0f, top + 20.0f, (float) width - 52.0f, 16.0f), juce::Justification::centredLeft, true);
            return;
        }
        if (selected)
        {
            g.setColour (t.panel2);
            g.fillRect (0, 0, width, height);
            g.setColour (t.accent);
            g.fillRect (0, 0, 2, height);
        }
        g.setColour (t.text);
        g.setFont (t.numFont (12.5f));
        g.drawText ("v" + juce::String (v.number), 12, 0, 56, height, juce::Justification::centredLeft, false);
        g.setFont (t.uiFont (12.5f));
        g.drawText (juce::Time (v.timeMs).formatted ("%d.%m.%Y  %H:%M"), 66, 0, 130, height, juce::Justification::centredLeft, false);
        g.setColour (t.dimText);
        g.drawText (kindText (v.kind), 200, 0, 120, height, juce::Justification::centredLeft, false);
        if (v.label.isNotEmpty())
        {
            g.setColour (t.accent);
            g.setFont (t.uiFont (12.5f, 600));
            g.drawText (v.label, 322, 0, width - 332, height, juce::Justification::centredLeft, true);
        }
    }

    void selectedRowsChanged (int) override { updateButtons(); }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { editLabel(); }

    static juce::String kindText (daw::project::VersionKind k)
    {
        switch (k)
        {
            case daw::project::VersionKind::save: return Strings::versions::kindSave;
            case daw::project::VersionKind::autosave: return Strings::versions::kindAutosave;
            case daw::project::VersionKind::restore: return Strings::versions::kindRestore;
        }
        return {};
    }

    void updateButtons()
    {
        const bool any = list.getSelectedRow() >= 0 && list.getSelectedRow() < (int) rows.size();
        labelButton.setEnabled (any);
        restoreButton.setEnabled (any);
        takeButton.setEnabled (any);
    }

    // Loads the chosen version into memory only (not into the engine), lists its tracks and takes the chosen one.
    void takeTrack()
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) rows.size())
            return;
        const int number = rows[(size_t) row].number;
        juce::String error;
        const auto older = session.loadVersionModel (number, error);
        if (older == nullptr)
        {
            showMessage (Strings::versions::title, error);
            return;
        }
        const auto tracks = transfer::listTracks (*older);
        if (tracks.empty())
        {
            showMessage (Strings::versions::title, Strings::versions::noTracks);
            return;
        }
        juce::PopupMenu menu;
        menu.addSectionHeader (Strings::versions::pickTrack (number));
        std::vector<std::pair<int64_t, juce::String>> choices;
        for (const auto& t : tracks)
        {
            choices.emplace_back (t.id, t.name);
            menu.addItem ((int) choices.size(), Strings::versions::trackLabel (t.name, t.numDevices, t.numSessionClips, t.numArrangementClips));
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&takeButton),
                            [safe = juce::Component::SafePointer<Content> (this), number, choices] (int result) {
                                if (result <= 0 || safe == nullptr)
                                    return;
                                const auto& [id, name] = choices[(size_t) result - 1];
                                const auto taken = safe->session.takeTrackFromVersion (number, id);
                                if (! taken.wasOk())
                                    showMessage (Strings::versions::title, taken.getErrorMessage());
                                else if (safe->report)
                                    safe->report (Strings::versions::trackTaken (name, number));
                            });
    }

    void editLabel()
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) rows.size())
            return;
        const auto v = rows[(size_t) row];
        promptText (Strings::versions::title, Strings::versions::labelPrompt (v.number), v.label,
                    [safe = juce::Component::SafePointer<Content> (this), number = v.number] (const juce::String& text) {
                        if (safe == nullptr)
                            return;
                        safe->session.setVersionLabel (number, text);
                        safe->refresh();
                    },
                    true);
    }

    void restore()
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) rows.size())
            return;
        const int number = rows[(size_t) row].number;
        confirm (Strings::versions::restoreTitle (number), Strings::versions::restoreText, Strings::versions::restoreOk,
                 [safe = juce::Component::SafePointer<Content> (this), number] (bool ok) {
                     if (! ok || safe == nullptr)
                         return;
                     const auto result = safe->session.restoreVersion (number);
                     if (! result.wasOk())
                         showMessage (Strings::versions::title, result.getErrorMessage());
                     else if (safe->report)
                         safe->report (Strings::versions::restored);
                     safe->refresh();
                 });
    }

    project::ProjectSession& session;
    std::function<void (const juce::String&)> report;
    std::vector<project::VersionRecord> rows;
    juce::ListBox list;
    juce::TextButton labelButton, restoreButton, takeButton, closeButton;
};

VersionWindow::VersionWindow (project::ProjectSession& s, std::function<void()> onClosed, std::function<void (const juce::String&)> onMessage)
    : DocumentWindow (Strings::versions::title + " · " + s.name(), theme().panel, DocumentWindow::closeButton), closed (std::move (onClosed))
{
    setUsingNativeTitleBar (true);
    content = new Content (s, std::move (onMessage));
    setContentOwned (content, true);
    setResizable (true, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

VersionWindow::~VersionWindow() = default;

void VersionWindow::refresh()
{
    content->refresh();
}

int VersionWindow::numRows() const
{
    return content->numRows();
}

void VersionWindow::selectRow (int row)
{
    content->selectRow (row);
}
} // namespace ui
