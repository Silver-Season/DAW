#pragma once

#include "Controls.h"

#include "../engine/OfflineRenderer.h"
#include "../model/ProjectModel.h"

namespace ui
{
// "Export…" (Cmd+Shift+R, docs/spec/04-export.md, task 4.2 in part): what range, which format and bit depth, peak
// normalisation, the tail; then a progress bar and the result (length, peak, "Show in Finder").
class ExportWindow final : public juce::DocumentWindow
{
public:
    ExportWindow (Settings&, ProjectModel&, AudioEngine&, juce::File defaultFolder, juce::String defaultName, std::function<void()> closed);
    ~ExportWindow() override;
    void closeButtonPressed() override;

private:
    class Content;
    std::function<void()> onClosed;
};
} // namespace ui
