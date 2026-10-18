#pragma once

#include "Controls.h"

#include "../model/ProjectModel.h"

namespace ui
{
// "Quantize…" (Cmd+Shift+U): grid, strength, swing and whether note ends are quantised too. Apply writes the
// settings into the project and quantises the selected notes of the open clip in one undo step.
class QuantiseWindow final : public juce::DocumentWindow
{
public:
    struct Hooks
    {
        std::function<ProjectModel::QuantiseSettings()> settings;
        std::function<void (const ProjectModel::QuantiseSettings&)> apply;
        std::function<void()> closed;
    };

    explicit QuantiseWindow (Hooks);
    void closeButtonPressed() override;

private:
    class Content;
    Hooks hooks;
};
} // namespace ui
