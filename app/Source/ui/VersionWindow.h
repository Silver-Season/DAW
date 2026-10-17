#pragma once

#include "../project/ProjectSession.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
// The list of the versions of the open project (task 1.7): newest first, with a label button and a restore button.
class VersionWindow final : public juce::DocumentWindow
{
public:
    VersionWindow (project::ProjectSession&, std::function<void()> onClosed, std::function<void (const juce::String&)> onMessage);
    ~VersionWindow() override;

    void refresh();
    // For the screenshot script and tests.
    int numRows() const;
    void selectRow (int row);

    void closeButtonPressed() override { closed(); }

private:
    class Content;
    Content* content = nullptr; // owned by the window
    std::function<void()> closed;
};
} // namespace ui
